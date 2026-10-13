#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kevin Ball
"""Generate breadboard.html for the two-node test rig: camera node and PIR node, each on a half-size breadboard."""
import pathlib

HERE = pathlib.Path(__file__).parent
OUT = HERE / "breadboard.html"
CSS = (HERE / "bb-style.css").read_text()

RED, BLK, ORG, GRN, BLU, YEL, WHT = "#d6392e", "#1b1b1b", "#e8821c", "#2f9e4f", "#2f6fd6", "#e6c619", "#f4f4f4"
ROWS = {"A": 150, "B": 170, "C": 190, "D": 210, "E": 230, "F": 290, "G": 310, "H": 330, "I": 350, "J": 370}
TP, TM, BM, BP = 84, 104, 416, 436


def X(c):
    return 70 + (c - 1) * 20


class BB:
    def __init__(self, rail_names):
        self.s = []
        self.add = self.s.append
        self.rails = rail_names
        self.board()

    def board(self):
        a = self.add
        a('<rect x="40" y="60" width="640" height="400" rx="8" class="bb"/>')
        a('<rect x="40" y="258" width="640" height="14" class="bbgap"/>')
        for c in range(1, 31):
            for y in list(ROWS.values()) + [TP, TM, BM, BP]:
                a(f'<circle cx="{X(c)}" cy="{y}" r="4.2" class="hl"/>')
        for c in (1, 5, 10, 15, 20, 25, 30):
            a(f'<text x="{X(c)}" y="132" text-anchor="middle" class="num">{c}</text>')
        for r, y in ROWS.items():
            a(f'<text x="52" y="{y + 3.5}" text-anchor="middle" class="num">{r}</text>')
        for y, k in ((TP, "p"), (TM, "m"), (BM, "m"), (BP, "p")):
            a(f'<line x1="56" y1="{y}" x2="664" y2="{y}" class="rl-{k}"/>')
        for y, n in zip((TP, TM, BM, BP), self.rails):
            a(f'<text x="676" y="{y + 3}" class="rail">{n}</text>')

    def xiao(self, c0, usb, line1, line2, used, lens=False):
        a = self.add
        left = X(c0) - 27
        right = X(c0 + 6) + 27
        if usb == "left":
            top = ["5V", "GND", "3V3", "D10", "D9", "D8", "D7"]
            bot = ["D0", "D1", "D2", "D3", "D4", "D5", "D6"]
            a(f'<rect x="{left-14}" y="228" width="16" height="44" rx="3" class="usb"/>')
        else:
            top = ["D6", "D5", "D4", "D3", "D2", "D1", "D0"]
            bot = ["D7", "D8", "D9", "D10", "3V3", "GND", "5V"]
            a(f'<rect x="{right-2}" y="228" width="16" height="44" rx="3" class="usb"/>')
        a(f'<rect x="{left}" y="180" width="{right-left}" height="140" rx="6" class="mod"/>')
        cx = (left + right) / 2
        if lens:
            lx = right - 40 if usb == "left" else left + 8
            a(f'<rect x="{lx}" y="214" width="32" height="32" rx="4" class="cam"/><circle cx="{lx+16}" cy="230" r="9" class="lens"/><circle cx="{lx+16}" cy="230" r="3.5" class="lens2"/>')
        a(f'<text x="{cx}" y="238" text-anchor="middle" class="mt">{line1}</text><text x="{cx}" y="253" text-anchor="middle" class="mt">{line2}</text>')
        for i, n in enumerate(top):
            a(f'<rect x="{X(c0+i)-4}" y="{ROWS["C"]-4}" width="8" height="8" class="pin"/>')
            a(f'<text x="{X(c0+i)}" y="205" text-anchor="middle" class="pn">{n}</text>')
            if n in used:
                a(f'<circle cx="{X(c0+i)}" cy="{ROWS["C"]}" r="8.5" class="ring"/>')
        for i, n in enumerate(bot):
            a(f'<rect x="{X(c0+i)-4}" y="{ROWS["G"]-4}" width="8" height="8" class="pin"/>')
            a(f'<text x="{X(c0+i)}" y="298" text-anchor="middle" class="pn">{n}</text>')
            if n in used:
                a(f'<circle cx="{X(c0+i)}" cy="{ROWS["G"]}" r="8.5" class="ring"/>')

    def sip(self, c0, names, title1, title2, rings):
        """Small module on row F (pins) with its body above, over the channel."""
        a = self.add
        n = len(names)
        x0 = X(c0) - 28
        w = (X(c0 + n - 1) - X(c0)) + 56
        if n == 4:
            w = 140
        a(f'<rect x="{x0}" y="211" width="{w}" height="89" rx="5" class="mod"/>')
        a(f'<text x="{x0 + w/2}" y="236" text-anchor="middle" class="mt">{title1}</text><text x="{x0 + w/2}" y="249" text-anchor="middle" class="pn">{title2}</text>')
        for i, nm in enumerate(names):
            a(f'<rect x="{X(c0+i)-4}" y="{ROWS["F"]-4}" width="8" height="8" class="pin"/>')
            a(f'<text x="{X(c0+i)}" y="281" text-anchor="middle" class="pn">{nm}</text>')
            if nm in rings:
                a(f'<circle cx="{X(c0+i)}" cy="{ROWS["F"]}" r="8.5" class="ring"/>')

    def wire(self, d, color, dash=False, ends=()):
        cls = "w dash" if dash else "w"
        self.add(f'<path d="{d}" class="wo"/><path d="{d}" class="{cls}" stroke="{color}"/>')
        for ex, ey in ends:
            self.add(f'<circle cx="{ex}" cy="{ey}" r="4.5" fill="{color}" class="we"/>')

    def text(self, x, y, s, cls="wl", anchor="start"):
        self.add(f'<text x="{x}" y="{y}" text-anchor="{anchor}" class="{cls}">{s}</text>')

    def cell(self, x, y, w, h, label, sub):
        self.add(f'<rect x="{x+10}" y="{y-26}" width="{w-20}" height="20" rx="3" fill="#e7e3d3" stroke="#8c8672"/><text x="{x+w/2}" y="{y-12}" text-anchor="middle" class="dk">JST-PH breakout</text>')
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h/2.4:.0f}" class="mod"/>')
        self.add(f'<rect x="{x+w}" y="{y+h/2-8}" width="8" height="16" rx="2" class="usb"/>')
        self.add(f'<text x="{x+w/2}" y="{y+h/2-2}" text-anchor="middle" class="mt">{label}</text><text x="{x+w/2}" y="{y+h/2+13}" text-anchor="middle" class="pn">{sub}</text>')

    def svg(self, aria, height=610):
        return f'<svg viewBox="0 0 760 {height}" role="img" aria-label="{aria}">\n' + "\n".join(self.s) + "\n</svg>"


# ------------------------------------------------------------------ camera node
cam = BB(["+5V", "GND", "GND", "VBAT"])
cam.xiao(2, "left", "XIAO ESP32-S3", "Sense · camera", {"5V", "GND", "D6"}, lens=True)
cam.sip(12, ["IN", "GND", "OUT", "EN"], "MiniBoost", "5 V · TPS61023", {"IN", "GND", "OUT", "EN"})
cam.xiao(23, "right", "XIAO nRF52840", "trigger board", {"D6", "D7", "GND"})
cam.add('<text x="572" y="276" text-anchor="middle" class="pn">USB to the computer: power + log</text>')
a = cam.add
a(f'<line x1="{X(14)}" y1="350" x2="{X(19)}" y2="350" class="lead"/>')
a('<rect x="352" y="341" width="50" height="18" rx="3" class="dio"/><rect x="390" y="341" width="7" height="18" class="band"/>')
a(f'<circle cx="{X(14)}" cy="350" r="4" class="leg"/><circle cx="{X(19)}" cy="350" r="4" class="leg"/>')
a('<text x="377" y="337" text-anchor="middle" class="pn2">D1 1N5819</text>')
for c, name, val, r in ((5, "C1", "470 µF", 17), (7, "C2", "10 µF", 12), (9, "C3", "0.1 µF", 11)):
    x = X(c)
    a(f'<line x1="{x-4}" y1="{TP}" x2="{x-4}" y2="62" class="lead"/><line x1="{x+4}" y1="{TM}" x2="{x+4}" y2="62" class="lead"/>')
    if c == 5:
        a(f'<circle cx="{x}" cy="46" r="{r}" class="cap"/><text x="{x}" y="49" text-anchor="middle" class="pn2">+</text>')
    else:
        a(f'<circle cx="{x}" cy="48" r="{r}" class="ceram"/>')
    a(f'<text x="{x}" y="12" text-anchor="middle" class="pn2">{name}</text><text x="{x}" y="24" text-anchor="middle" class="pn2">{val}</text>')
cam.cell(60, 506, 150, 54, "18650 cell", "2200 mAh, JST lead")
cam.wire(f"M{X(1)},{BP} C 70,462 90,470 90,486", RED, ends=[(X(1), BP), (90, 486)])
cam.wire(f"M{X(2)},{BM} C 90,446 150,460 150,486", BLK, ends=[(X(2), BM), (150, 486)])
cam.wire(f"M{X(1)},{TM} C 22,{TM} 22,{BM} {X(1)},{BM}", BLK, ends=[(X(1), TM), (X(1), BM)])
cam.wire(f"M{X(2)},{ROWS['A']} C 66,128 66,98 {X(2)},{TP}", ORG, ends=[(X(2), ROWS["A"]), (X(2), TP)])
cam.wire(f"M{X(3)},{ROWS['A']} L {X(3)},{TM}", BLK, ends=[(X(3), ROWS["A"]), (X(3), TM)])
cam.wire(f"M{X(12)},{ROWS['G']} C 268,360 268,400 {X(12)},{BP}", RED, ends=[(X(12), ROWS["G"]), (X(12), BP)])
cam.wire(f"M{X(13)},{ROWS['G']} C 300,350 300,390 {X(13)},{BM}", BLK, ends=[(X(13), ROWS["G"]), (X(13), BM)])
cam.wire(f"M{X(19)},{ROWS['G']} C 452,250 452,140 {X(19)},{TP}", ORG, ends=[(X(19), ROWS["G"]), (X(19), TP)])
cam.wire(f"M{X(15)},{ROWS['G']} C 392,330 500,230 {X(23)},{ROWS['A']}", GRN, ends=[(X(15), ROWS["G"]), (X(23), ROWS["A"])])
cam.wire(f"M{X(8)},{ROWS['H']} C 300,318 420,318 {X(23)},{ROWS['H']}", BLU, ends=[(X(8), ROWS["H"]), (X(23), ROWS["H"])])
cam.wire(f"M{X(28)},{ROWS['H']} C 604,360 604,390 {X(28)},{BM}", BLK, ends=[(X(28), ROWS["H"]), (X(28), BM)])
cam.text(440, 212, "EN")
cam.text(470, 352, "UART TX → RX", anchor="middle")

camera_rows = [
    ("1", "18650 + (JST breakout)", "VBAT rail, col 1", "red", "VBAT"),
    ("2", "18650 − (JST breakout)", "GND rail (bottom), col 2", "black", "GND"),
    ("3", "GND tie", "GND rail top ↔ bottom, col 1", "black", "GND"),
    ("4", "Camera 5V pin (A2)", "+5V rail (top), col 2", "orange", "+5V_CAM"),
    ("5", "Camera GND pin (A3)", "GND rail (top), col 3", "black", "GND"),
    ("6", "MiniBoost IN (G12)", "VBAT rail, col 12", "red", "VBAT"),
    ("7", "MiniBoost GND (G13)", "GND rail (bottom), col 13", "black", "GND"),
    ("8", "Schottky D1", "anode I14 (boost OUT), cathode I19", "part", "BOOST_OUT → +5V_CAM"),
    ("9", "Cathode strip (G19)", "+5V rail (top), col 19", "orange", "+5V_CAM"),
    ("10", "MiniBoost EN (G15)", "trigger nRF D6 (A23)", "green", "BOOST_EN"),
    ("11", "Camera D6 TX (H8)", "trigger nRF D7 RX (H23)", "blue", "UART, camera to nRF"),
    ("12", "Trigger nRF GND (H28)", "GND rail (bottom), col 28", "black", "GND"),
    ("13", "C1 470 µF, C2 10 µF, C3 0.1 µF", "+5V rail ↔ GND rail, cols 5, 7, 9", "parts", "5 V decoupling"),
]

# ------------------------------------------------------------------ PIR node
pir = BB(["unused", "GND", "GND", "VBAT"])
pir.xiao(2, "left", "XIAO nRF52840", "PIR node", {"3V3", "GND", "D0"})
pir.add('<text x="150" y="276" text-anchor="middle" class="pn">BAT+ / BAT- pads on the back</text>')
pir.sip(18, ["GND", "OUT", "VCC"], "PIR", "Adafruit mini PIR", {"GND", "OUT", "VCC"})
pir.cell(60, 506, 150, 54, "18650 cell", "2200 mAh, JST lead")
pir.wire(f"M{X(1)},{BP} C 70,462 90,470 90,486", RED, ends=[(X(1), BP), (90, 486)])
pir.wire(f"M{X(2)},{BM} C 90,446 150,460 150,486", BLK, ends=[(X(2), BM), (150, 486)])
pir.wire(f"M{X(1)},{TM} C 22,{TM} 22,{BM} {X(1)},{BM}", BLK, ends=[(X(1), TM), (X(1), BM)])
pir.wire(f"M{X(3)},{ROWS['A']} L {X(3)},{TM}", BLK, ends=[(X(3), ROWS["A"]), (X(3), TM)])
pir.wire(f"M{X(4)},{ROWS['A']} C 300,120 460,300 {X(20)},{ROWS['G']}", WHT, ends=[(X(4), ROWS["A"]), (X(20), ROWS["G"])])
pir.wire(f"M{X(2)},{ROWS['H']} C 200,372 340,372 {X(19)},{ROWS['G']}", YEL, ends=[(X(2), ROWS["H"]), (X(19), ROWS["G"])])
pir.wire(f"M{X(18)},{ROWS['G']} C 380,350 380,390 {X(18)},{BM}", BLK, ends=[(X(18), ROWS["G"]), (X(18), BM)])
pir.wire(f"M170,322 C 166,370 166,400 {X(6)},{BP}", RED, dash=True, ends=[(X(6), BP)])
pir.wire(f"M190,322 C 194,360 194,390 {X(7)},{BM}", BLK, dash=True, ends=[(X(7), BM)])
pir.text(300, 118, "3V3", anchor="middle")
pir.text(300, 366, "PIR OUT", anchor="middle")

pir_rows = [
    ("1", "18650 + (JST breakout)", "VBAT rail, col 1", "red", "VBAT"),
    ("2", "18650 − (JST breakout)", "GND rail (bottom), col 2", "black", "GND"),
    ("3", "GND tie", "GND rail top ↔ bottom, col 1", "black", "GND"),
    ("4", "nRF GND pin (A3)", "GND rail (top), col 3", "black", "GND"),
    ("5", "nRF 3V3 pin (A4)", "PIR VCC (G20)", "white", "+3V3"),
    ("6", "nRF D0 (H2)", "PIR OUT (G19)", "yellow", "PIR_OUT"),
    ("7", "PIR GND (G18)", "GND rail (bottom), col 18", "black", "GND"),
    ("8", "nRF BAT+ pad (back)", "VBAT rail, col 6", "red, dashed", "VBAT"),
    ("9", "nRF BAT− pad (back)", "GND rail (bottom), col 7", "black, dashed", "GND"),
]


def rows_html(rows):
    return "\n".join(
        f"<tr><td>{a}</td><td>{b}</td><td>{c}</td><td><span class='sw {d.split(',')[0].split()[0]}'></span>{d}</td><td>{e}</td></tr>"
        for a, b, c, d, e in rows
    )


TABLE_HEAD = "<thead><tr><th>#</th><th>From</th><th>To</th><th>Wire</th><th>Net</th></tr></thead>"

HTML = (
    "<title>Early-Warning Breadboard</title>\n"
    '<link rel="preconnect" href="https://fonts.googleapis.com">\n'
    '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;600&display=swap">\n'
    f"<style>{CSS}</style>\n"
    """
<main>
  <header>
    <div class="tag">Two-node test rig · breadboard layout</div>
    <h1>Early-warning breadboard</h1>
    <p class="lede">The rig from the test plan, on two half-size breadboards: a camera node with one nRF (the trigger board) and a PIR node 20 m away. MeshCore is left out on purpose. The camera's verdict goes to the trigger nRF, which writes everything to a USB serial log.</p>
  </header>

  <section>
    <h2>Camera node</h2>
    <figure>
      <div class="scroll">
"""
    + cam.svg("Top view of a half-size breadboard for the camera node. The camera XIAO ESP32-S3 sits at the left, the MiniBoost in the middle, and the trigger XIAO nRF52840 at the right with USB to the computer. An 18650 cell feeds the bottom rails. Wires carry battery, 5 volt, ground, boost enable and UART.")
    + """
      </div>
      <div class="legend" aria-hidden="true">
        <span><i class="sw red"></i>VBAT</span><span><i class="sw orange"></i>+5V to camera</span><span><i class="sw black"></i>GND</span>
        <span><i class="sw green"></i>boost EN</span><span><i class="sw blue"></i>UART</span>
      </div>
      <figcaption>Columns run 1 to 30; rows A to E sit above the centre channel and F to J below. The XIAOs straddle the channel on rows C and G, and the MiniBoost sits on row F with its body over the channel. Orange rings mark pins that carry a wire. The trigger nRF stays on USB for the log, while the camera runs from the 18650 through the boost so its power-up is real. The camera's own USB is for flashing only; the Schottky stops the boost from being back-fed. The cell's JST lead plugs into a small JST-PH breakout (Adafruit ID 1862), which gives breadboard pins for wires 1 and 2.</figcaption>
    </figure>
    <div class="scroll">
      <table>
"""
    + TABLE_HEAD
    + "<tbody>\n"
    + rows_html(camera_rows)
    + """
      </tbody></table>
    </div>
  </section>

  <section>
    <h2>PIR node</h2>
    <figure>
      <div class="scroll">
"""
    + pir.svg("Top view of a half-size breadboard for the PIR node. A XIAO nRF52840 sits at the left, powered from an 18650 cell through its back battery pads, and a PIR module sits to its right. The nRF 3V3 pin feeds the PIR, and the PIR output goes to nRF pin D0.")
    + """
      </div>
      <div class="legend" aria-hidden="true">
        <span><i class="sw red"></i>VBAT</span><span><i class="sw black"></i>GND</span><span><i class="sw white"></i>3V3</span><span><i class="sw yellow"></i>PIR out</span>
      </div>
      <figcaption>The PIR node is a plain XIAO nRF52840, no LoRa radio. It runs from the 18650 through the back battery pads, so the module's own 3V3 regulator feeds the PIR. USB is only for flashing and for checking logs at the bench. The PIR faces down-trail on the test stand. The PIR is Adafruit's breadboard-friendly mini PIR (ID 4871), whose pins read GND, OUT, power left to right; it takes 3 to 12 V through its own regulator and gives a 3.3 V output that stays high about 2 s.</figcaption>
    </figure>
    <div class="scroll">
      <table>
"""
    + TABLE_HEAD
    + "<tbody>\n"
    + rows_html(pir_rows)
    + """
      </tbody></table>
    </div>
    <p class="small">Holes are named column then row, so A2 is column 2, row A. A half-size breadboard has 30 columns; use rails that are continuous (not split).</p>
  </section>

  <section>
    <h2>Charging the cells</h2>
    <p>The Adafruit bq24074 charger (ID 4755) charges the cells off the breadboard. Plug a cell's JST lead into its battery socket and power it from USB-C. It charges at 1 A by default (500 mA or 1.5 A by solder jumper), against about 100 mA from the XIAO's own charger, so a 2200 mAh cell takes a few hours instead of about a day. Charge both cells before the first run. It also has a JST load socket that can power a node while a cell tops up, and the same board is the charger in the rev B carrier design.</p>
  </section>

  <section>
    <h2>What the firmware does on this rig</h2>
    <ul class="steps">
      <li><b>PIR node:</b> sends a trigger packet on motion, and a heartbeat every few seconds. Each packet carries a sequence number.</li>
      <li><b>Trigger nRF:</b> logs every packet with its time, sequence number and RSSI, raises EN on a trigger, then logs the camera's boot message, first frame and verdict from the UART line, and drops EN at the end of the window.</li>
      <li><b>Camera:</b> boots when the boost comes up, runs detection, and prints its boot, frame and verdict messages on D6.</li>
    </ul>
  </section>

  <section>
    <h2>Bring-up order</h2>
    <ol class="steps">
      <li>Charge both cells. Then the PIR node alone: flash it, check the PIR output on D0, and measure sleep and transmit current.</li>
      <li>Camera node, boost only: build the rails, the boost and the Schottky (wires 1, 2, 3, 6, 7, 9). With a meter, check the output is 0 V with EN low and about 5 V with EN high.</li>
      <li>Add the trigger nRF and EN (wires 10, 12), flash it, and confirm it receives the PIR node's packets and switches the boost.</li>
      <li>Add the camera (wires 4, 5, 11), flash the timing firmware, and read the boot-to-verdict log. Then move the PIR node out to 20 m for the link tests.</li>
    </ol>
  </section>

  <section>
    <h2>Left out of this rig</h2>
    <ul class="steps">
      <li><b>MeshCore nRF and the Wio-SX1262.</b> It joins last, after the link, boost and camera are proven.</li>
      <li><b>IR gate.</b> It needs a TO-220 MOSFET and spare columns, and the detection chain should work first.</li>
      <li><b>Charger and solar on the board.</b> The bq24074 charges the cells on its own, off the breadboard. It joins the carrier in rev B, with solar.</li>
    </ul>
  </section>

  <p class="note">Worked out from the design notes, not from datasheets. The XIAO pin orientation is derived from the module rotation (USB left or right), and the 0.6 inch pin-row spacing and the MiniBoost pin order (IN, GND, OUT, EN) are assumptions. Check these against the real boards before powering anything. Also check whether the MiniBoost's EN pin floats high or low by itself, since an nRF pin that is not driven yet could switch it. The cells are protected 18650s with a JST lead, so check the breakout's + and − marking against the cell's red and black wires before connecting.</p>
</main>
"""
)
OUT.write_text(HTML)
print(OUT, len(HTML))
