# Carrier board hardware (KiCad)

Rev B all-in-one carrier for the runner early-warning node. Design and decisions:
`~/code/brain/notes/ares/2026-10/runner-early-warning-node.md` (Stage 6).

- `ew-carrier/` is the KiCad 10 project (schematic so far; PCB not started).
- `gen_schematic.py` generated the first schematic from a net list. Once the schematic is edited
  in KiCad, edit it there and stop re-running the script (it overwrites `ew-carrier.kicad_sch`).

KiCad is installed at `~/Applications/KiCad` (the Homebrew cask needs sudo, so it was copied from
the verified disk image instead). CLI: `~/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`.

Checks used so far:

```
kicad-cli sch erc --severity-all ew-carrier/ew-carrier.kicad_sch
kicad-cli sch export bom ew-carrier/ew-carrier.kicad_sch
```

Unverified before layout: Adafruit MiniBoost pin order/pitch, bq24074 header names, AM312 pin order,
XIAO row spacing, IRLZ44N as the IR switch.
