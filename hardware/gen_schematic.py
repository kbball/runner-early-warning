#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kevin Ball
"""Generate the KiCad schematic for the runner early-warning carrier (rev B).

This writes ew-carrier/ew-carrier.kicad_sch from the net list below, pulling symbol
definitions out of the installed KiCad libraries. It is a starting point: once the
schematic is being edited in KiCad, edit it there and stop re-running this script,
or the edits are overwritten.

Design notes: ~/code/brain/notes/ares/2026-10/runner-early-warning-node.md (Stage 6).
Everything here is through-hole / socketed. Module pin orders and pitches are
unverified (see the notes' open items) and are marked in the part values.
"""
import pathlib
import sys
import uuid

KICAD_APP = pathlib.Path.home() / "Applications/KiCad/KiCad.app/Contents/SharedSupport"
SYMBOLS = KICAD_APP / "symbols"
OUT_DIR = pathlib.Path(__file__).parent / "ew-carrier"
PROJECT = "ew-carrier"

# Match the installed KiCad (set from a template file at run time if present).
FILE_VERSION = "20250114"
GENERATOR_VERSION = "9.0"


# ---------------------------------------------------------------- s-expression
class Str(str):
    """A quoted string token (kept raw, so escapes survive a round trip)."""


def tokenize(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c in "()":
            yield c
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while text[j] != '"':
                if text[j] == "\\":
                    buf.append(text[j : j + 2])
                    j += 2
                else:
                    buf.append(text[j])
                    j += 1
            yield Str("".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and text[j] not in " \t\r\n()":
                j += 1
            yield text[i:j]
            i = j


def parse(text):
    stack, cur = [], []
    for tok in tokenize(text):
        if tok == "(" and not isinstance(tok, Str):
            stack.append(cur)
            cur = []
        elif tok == ")" and not isinstance(tok, Str):
            done = cur
            cur = stack.pop()
            cur.append(done)
        else:
            cur.append(tok)
    return cur[0]


def ser(node):
    if isinstance(node, Str):
        return '"' + node + '"'
    if isinstance(node, str):
        return node
    return "(" + " ".join(ser(c) for c in node) + ")"


def find_all(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def find(node, name):
    got = find_all(node, name)
    return got[0] if got else None


_lib_cache = {}


def _raw_symbol(lib, name):
    if lib not in _lib_cache:
        _lib_cache[lib] = parse((SYMBOLS / f"{lib}.kicad_sym").read_text())
    for sym in find_all(_lib_cache[lib], "symbol"):
        if sym[1] == name:
            return sym
    sys.exit(f"symbol {lib}:{name} not found")


def _flatten(lib, name):
    """Return the symbol with any 'extends' parent folded in, named `name`."""
    sym = _raw_symbol(lib, name)
    ext = find(sym, "extends")
    if not ext:
        return sym
    parent = _flatten(lib, str(ext[1]))
    pname = str(parent[1])
    child_props = {str(p[1]): p for p in find_all(sym, "property")}
    out = [parent[0], Str(name)]
    for c in parent[2:]:
        if isinstance(c, list) and c and c[0] == "property":
            out.append(child_props.get(str(c[1]), c))
        elif isinstance(c, list) and c and c[0] == "symbol":
            sub = list(c)
            sub[1] = Str(str(sub[1]).replace(pname, name, 1))
            out.append(sub)
        else:
            out.append(c)
    return out


def load_symbol(lib, name):
    """Return (symbol_node_renamed_to_Lib:Name, {pin_number: (x, y, angle)})."""
    sym = _flatten(lib, name)
    pins = {}

    def walk(n):
        for c in n:
            if isinstance(c, list) and c:
                if c[0] == "pin":
                    at = find(c, "at")
                    num = find(c, "number")
                    pins[str(num[1])] = (float(at[1]), float(at[2]), int(float(at[3])))
                else:
                    walk(c)

    walk(sym)
    renamed = [sym[0], Str(f"{lib}:{name}")] + sym[2:]
    return renamed, pins


# ---------------------------------------------------------------- schematic
def uid():
    return str(uuid.uuid4())


def fmt(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


ROOT_UUID = uid()
lib_symbols = {}  # lib_id -> renamed symbol node
body = []  # schematic items, as strings


def need(lib, name):
    lib_id = f"{lib}:{name}"
    if lib_id not in lib_symbols:
        lib_symbols[lib_id] = load_symbol(lib, name)
    return lib_symbols[lib_id]


def effects(size=1.27, hide=False, justify=None):
    parts = [f"(font (size {size} {size}))"]
    if justify:
        parts.append(f"(justify {justify})")
    if hide:
        parts.append("(hide yes)")
    return "(effects " + " ".join(parts) + ")"


OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}  # schematic y is down
LABEL_ANGLE = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}
LABEL_JUSTIFY = {180: "right bottom", 0: "left bottom", 270: "right bottom", 90: "left bottom"}

STUB = 2.54
net_pins = {}  # net -> list of "ref.pin" for the report


def place(lib, name, ref, value, footprint, x, y, nets, datasheet="~", note=None):
    """Place a symbol and attach a net label (or no-connect) to each pin.

    nets: {pin_number: net_name or None}; None means a no-connect marker.
    Pins not mentioned are left unconnected and get a no-connect marker.
    """
    lib_id = f"{lib}:{name}"
    _, pins = need(lib, name)
    ys = [p[1] for p in pins.values()] or [0]
    top = max(ys) + 4.0
    bottom = -min(ys) + 4.0
    props = [
        f'(property "Reference" "{ref}" (at {fmt(x)} {fmt(y - top)} 0) {effects()})',
        f'(property "Value" "{value}" (at {fmt(x)} {fmt(y + bottom)} 0) {effects()})',
        f'(property "Footprint" "{footprint}" (at {fmt(x)} {fmt(y)} 0) {effects(hide=True)})',
        f'(property "Datasheet" "{datasheet}" (at {fmt(x)} {fmt(y)} 0) {effects(hide=True)})',
    ]
    pin_items = "".join(f'(pin "{n}" (uuid "{uid()}"))' for n in pins)
    body.append(
        f'(symbol (lib_id "{lib_id}") (at {fmt(x)} {fmt(y)} 0) (unit 1) '
        f"(exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid \"{uid()}\") "
        + " ".join(props)
        + " "
        + pin_items
        + f' (instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference "{ref}") (unit 1)))))'
    )
    for num, (px, py, ang) in pins.items():
        net = nets.get(num)
        ex, ey = x + px, y - py  # connection point in schematic coordinates
        if net is None:
            body.append(f'(no_connect (at {fmt(ex)} {fmt(ey)}) (uuid "{uid()}"))')
            continue
        dx, dy = OUTWARD[ang]
        sx, sy = ex + dx * STUB, ey + dy * STUB
        body.append(
            f"(wire (pts (xy {fmt(ex)} {fmt(ey)}) (xy {fmt(sx)} {fmt(sy)})) "
            f'(stroke (width 0) (type default)) (uuid "{uid()}"))'
        )
        la = LABEL_ANGLE[(dx, dy)]
        body.append(
            f'(label "{net}" (at {fmt(sx)} {fmt(sy)} {la}) '
            f'{effects(justify=LABEL_JUSTIFY[la])} (uuid "{uid()}"))'
        )
        net_pins.setdefault(net, []).append(f"{ref}.{num}")
    if note:
        text(x - 5.08, y + bottom + 2.54, note)


def text(x, y, s, size=1.27):
    s = s.replace("\n", "\\n")
    body.append(
        f'(text "{s}" (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0) '
        f"{effects(size=size, justify='left top')} (uuid \"{uid()}\"))"
    )


PIN_SOCKET_1x07 = "Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical"
PIN_SOCKET_1x04 = "Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical"
PIN_SOCKET_1x03 = "Connector_PinSocket_2.54mm:PinSocket_1x03_P2.54mm_Vertical"
PIN_HEADER_1x03 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
PIN_HEADER_1x02 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
JST_PH_2 = "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical"

CONN = "Connector_Generic"


def build():
    text(
        12.7, 12.7,
        "Runner early-warning carrier, rev B (concept schematic, 2026-10-12)\n"
        "Through-hole and socketed. Net labels carry the connectivity. Pin orders and pitches of the\n"
        "Adafruit modules are unverified; check them against the real boards before layout.",
        size=1.8,
    )

    # ---- power: solar in, charger, boost, cell feed
    text(12.7, 40.0, "POWER IN / CHARGER / BOOST", size=1.6)
    place(CONN, "Conn_01x02", "J7", "SOLAR 6V+ (JST-PH)", JST_PH_2, 38.1, 58.42,
          {"1": "SOLAR_IN", "2": "GND"})
    place(CONN, "Conn_01x03", "J6", "bq24074 header", PIN_SOCKET_1x03, 38.1, 88.9,
          {"1": "VBAT_LOAD", "2": "GND", "3": "SOLAR_IN"},
          note="1 LOAD  2 GND  3 DC IN\n(cell plugs into the module's JST)")
    place(CONN, "Conn_01x04", "J5", "MiniBoost 5V (TPS61023)", PIN_SOCKET_1x04, 38.1, 119.38,
          {"1": "BOOST_OUT", "2": "GND", "3": "VBAT_LOAD", "4": "BOOST_EN"},
          note="1 OUT  2 GND  3 IN  4 EN\n(order unverified)")
    place(CONN, "Conn_01x02", "J10", "nRF BAT feed (to XIAO back pads)", PIN_HEADER_1x02, 38.1, 152.4,
          {"1": "VBAT_LOAD", "2": "GND"})

    # ---- camera 5 V rail: Schottky, bulk cap, ceramics
    text(88.9, 40.0, "CAMERA 5 V RAIL", size=1.6)
    place("Device", "D_Schottky", "D1", "1N5819-class, standing",
          "Diode_THT:D_DO-41_SOD81_P3.81mm_Vertical_CathodeUp", 99.06, 63.5,
          {"1": "+5V_CAM", "2": "BOOST_OUT"})
    place("Device", "C_Polarized", "C1", "470uF 10V",
          "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm", 124.46, 63.5,
          {"1": "+5V_CAM", "2": "GND"})
    place("Device", "C", "C2", "10uF",
          "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm", 144.78, 63.5,
          {"1": "+5V_CAM", "2": "GND"})
    place("Device", "C", "C3", "0.1uF",
          "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm", 162.56, 63.5,
          {"1": "+5V_CAM", "2": "GND"})

    # ---- IR gate (night ops)
    text(88.9, 100.0, "IR GATE (NIGHT OPS)", size=1.6)
    place("Device", "R", "R1", "100R", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
          99.06, 124.46, {"1": "IR_GATE", "2": "IR_GATE_G"})
    place("Device", "R", "R2", "100k pulldown", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
          119.38, 124.46, {"1": "IR_GATE_G", "2": "GND"})
    place("Transistor_FET", "IRLZ44N", "Q1", "IRLZ44N (logic-level, TBD)",
          "Package_TO_SOT_THT:TO-220-3_Vertical", 147.32, 124.46,
          {"1": "IR_GATE_G", "2": "IR_SINK", "3": "GND"})
    place(CONN, "Conn_01x02", "J8", "IR LED (JST-PH)", JST_PH_2, 170.18, 124.46,
          {"1": "+5V_CAM", "2": "IR_SINK"})

    # ---- camera XIAO ESP32-S3 Sense
    text(200.66, 40.0, "CAMERA: XIAO ESP32-S3 Sense", size=1.6)
    place(CONN, "Conn_01x07", "J1", "XIAO cam, left row D0-D6", PIN_SOCKET_1x07, 220.98, 63.5,
          {"1": None, "2": "IR_GATE", "3": None, "4": None, "5": None, "6": None, "7": "CAM_TX"},
          note="1 D0  2 D1 (IR gate)  3 D2  4 D3  5 D4  6 D5  7 D6 (TX)")
    place(CONN, "Conn_01x07", "J2", "XIAO cam, right row 5V..D7", PIN_SOCKET_1x07, 220.98, 111.76,
          {"1": "+5V_CAM", "2": "GND", "3": None, "4": None, "5": None, "6": None, "7": None},
          note="1 5V  2 GND  3 3V3  4 D10  5 D9  6 D8  7 D7")

    # ---- controller XIAO nRF52840 + Wio-SX1262
    text(274.32, 40.0, "CONTROLLER: XIAO nRF52840 + Wio-SX1262", size=1.6)
    place(CONN, "Conn_01x07", "J3", "XIAO nRF, left row D0-D6", PIN_SOCKET_1x07, 294.64, 63.5,
          {"1": "PIR_OUT", "2": None, "3": None, "4": None, "5": None, "6": None, "7": "BOOST_EN"},
          note="1 D0 (PIR)  2-6 D1-D5 radio  7 D6 (boost EN)")
    place(CONN, "Conn_01x07", "J4", "XIAO nRF, right row 5V..D7", PIN_SOCKET_1x07, 294.64, 111.76,
          {"1": None, "2": "GND", "3": "+3V3_NRF", "4": None, "5": None, "6": None, "7": "CAM_TX"},
          note="1 5V  2 GND  3 3V3  4-6 D10-D8 radio  7 D7 (UART RX)")

    # ---- PIR
    text(274.32, 150.0, "PIR (BACK FACE)", size=1.6)
    place(CONN, "Conn_01x03", "J9", "AM312 PIR pads", PIN_HEADER_1x03, 294.64, 172.72,
          {"1": "+3V3_NRF", "2": "PIR_OUT", "3": "GND"},
          note="1 VCC  2 OUT  3 GND  (order unverified)")

    # ---- mounting holes
    for i, (hx, hy) in enumerate([(12.7, 270.0), (35.56, 270.0), (58.42, 270.0), (81.28, 270.0)], start=1):
        place("Mechanical", "MountingHole", f"H{i}", "M3", "MountingHole:MountingHole_3.2mm_M3", hx, hy, {})


FOOTPRINT_LIBS = [
    "Capacitor_THT", "Connector_JST", "Connector_PinHeader_2.54mm", "Connector_PinSocket_2.54mm",
    "Diode_THT", "MountingHole", "Package_TO_SOT_THT", "Resistor_THT",
]


def write_lib_tables():
    """Project-local library tables, so the project resolves the stock KiCad libraries."""
    syms = sorted({lib_id.split(":")[0] for lib_id in lib_symbols})
    rows = "".join(
        f'  (lib (name "{n}") (type "KiCad") (uri "${{KICAD10_SYMBOL_DIR}}/{n}.kicad_sym") (options "") (descr ""))\n'
        for n in syms
    )
    (OUT_DIR / "sym-lib-table").write_text(f"(sym_lib_table\n  (version 7)\n{rows})\n")
    rows = "".join(
        f'  (lib (name "{n}") (type "KiCad") (uri "${{KICAD10_FOOTPRINT_DIR}}/{n}.pretty") (options "") (descr ""))\n'
        for n in FOOTPRINT_LIBS
    )
    (OUT_DIR / "fp-lib-table").write_text(f"(fp_lib_table\n  (version 7)\n{rows})\n")


def write():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    build()
    libs = "".join(ser(node) + "\n" for node, _ in lib_symbols.values())
    sch = (
        f'(kicad_sch (version {FILE_VERSION}) (generator "eeschema") (generator_version "{GENERATOR_VERSION}") '
        f'(uuid "{ROOT_UUID}") (paper "A3")\n'
        '(title_block (title "Runner early-warning carrier") (date "2026-10-12") (rev "B-concept") '
        '(company "") (comment 1 "Generated by hardware/gen_schematic.py; edit in KiCad from here on"))\n'
        f"(lib_symbols\n{libs})\n"
        + "\n".join(body)
        + '\n(sheet_instances (path "/" (page "1")))\n(embedded_fonts no)\n)\n'
    )
    (OUT_DIR / f"{PROJECT}.kicad_sch").write_text(sch)
    pro = OUT_DIR / f"{PROJECT}.kicad_pro"
    if not pro.exists():
        pro.write_text('{\n  "meta": {\n    "filename": "%s.kicad_pro",\n    "version": 3\n  }\n}\n' % PROJECT)
    write_lib_tables()
    print(f"wrote {OUT_DIR / (PROJECT + '.kicad_sch')}")
    for net in sorted(net_pins):
        print(f"{net:12s} {' '.join(net_pins[net])}")


if __name__ == "__main__":
    write()
