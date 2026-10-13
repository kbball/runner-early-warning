# Camera node camera firmware

XIAO ESP32-S3 Sense, built on **ESP-IDF** (not Arduino): the person model is Espressif's ESP-DL, which needs
ESP-IDF 5.3 or newer. The node is powered only while the trigger board holds the boost EN high, so every run starts
from a cold boot. On boot it reports on its UART, grabs frames for a session, reports a verdict, and sleeps.

Design: `~/code/brain/notes/ares/2026-10/runner-early-warning-two-node.md` (sections 2, 10, 11, 12).

## Behaviour (node mode)

1. Opens the hardware UART (D7 RX, D6 TX, GPIO44/43, 115200) and sends `EW,BOOT,<ms>` **first**, so the trigger board
   sees the camera come up. The debug log goes to USB, not the UART.
2. Loads the settings from flash, initialises the camera (RGB565, QVGA, two frame buffers in PSRAM) and loads the
   person model from its flash partition.
3. Grabs frames for the session length (default 30 s), sending `EW,FRAME,<ms>,<n>` for the first 5 frames and every 10th.
4. Scores each frame with ESP-DL (about 166 ms a frame, so roughly 6 fps). A `PERSON` verdict needs the configured
   number of consecutive frames (default 2) at or above the threshold (default 60) and ends the session early.
5. Sends `EW,VERDICT,<ms>,PERSON|NONE,<best score>` then `EW,DONE,<ms>`, records the session in the flash log, waits
   3 s for the boost to drop, then deep sleeps (no wake source, so it stays down until power returns).

Line formats are produced by `../common/lib/CameraMsg` (the same code the trigger board parses with) and unit
tested on the host: `cd ../common && pio test -e native`.

## Setup mode and field tuning

The threshold, frames-in-a-row and session length live in flash and are changed from a web page, so tuning in the
field needs no reflash. Setup mode is entered at power-up when **D1 (GPIO2) reads high**; D1 has an internal
pull-down, so with nothing connected the board runs as the node.

- On the carrier this is the SETUP/RUN switch: it ties the trigger board's 3V3 to D1 (through 10 k, with a 100 k
  pull-down) and, through a Schottky diode, forces the boost EN high so the camera powers up without a trigger.
  On the breadboard, a jumper from 3V3 to D1 and a reset is the same thing.
- The board becomes the Wi-Fi access point `ew-camera` (password `ew-camera-view`, no internet). Open
  `http://192.168.4.1/`: a live view with detection boxes and scores, the settings form (Save / Defaults), and the
  log of the last 16 real sessions (first-frame time, frames, best score, verdict).
- The yellow LED blinks while in setup. A switch-entered setup ends after 15 minutes and the board deep-sleeps.
- Join with the 2.4 GHz antenna fitted; without it the access point is too weak to use.

The bench env `camera_view` is always in setup mode (no switch, no timeout): `pio run -e camera_view -t upload`.

## Test variables

Set with `-D` in `platformio.ini`: `STUB_FORCE_PERSON=1` skips the model and reports PERSON after a few frames, to
test the rest of the chain with nobody in front of the camera; `MESHCORE_FRAMES=1` also sends MeshCore companion frames
on a confirmed person, on the same UART TX line (off by default: the MeshCore nRF needs its UART enabled first; the
timestamp is `MESH_EPOCH_BASE` plus uptime because the camera has no clock). `PERSON_THRESHOLD`,
`PERSON_CONFIRM_FRAMES` and `SESSION_MS` are only the defaults of the flash settings.

The old microSD frame saving (`SAVE_FRAMES`) was dropped in the ESP-IDF port; the session log covers tuning for now.

## Wiring (matches the breadboard page)

| Camera pin | Goes to |
|---|---|
| D6 (TX, GPIO43) | trigger nRF D7 (RX) |
| D1 (GPIO2) | the SETUP switch (high = setup mode); leave open to run as the node |
| 5V | the 5 V rail from the MiniBoost, through the Schottky |
| GND | common GND |

The camera's own USB-C is for flashing and the debug log only. With the boost off the rail, the board powers from USB.

## Build and flash

```
pio run                      # builds the node (first build downloads ESP-IDF components; needs the network)
pio run -t upload            # app + partition table + the person model, in one go
pio device monitor           # debug log on USB serial; the EW,... lines are on D6, not here
```

The node deep-sleeps after each session, which drops USB. To flash it, put it in download mode: with USB plugged in,
hold **B**, tap **R**, release B (macOS may ask to allow the accessory). The log is only visible if a reader is open
before the board boots, so open the monitor first, then press R. PlatformIO's bundled CMake (3.16) is too old for
ESP-DL, so `platformio.ini` pins `platformio/tool-cmake@3.30.2`. The person model is flashed by `model_partition.py`
because PlatformIO cannot run the component's own embed step.

## Measured on hardware (2026-10-13, node mode, nothing in front of the camera)

Camera init 334 ms, model load 133 ms, first frame 689 ms by the app clock (add the ROM and bootloader time, roughly
100-200 ms, for power-to-first-frame), about 166 ms per frame, 179 frames in a 30 s session. RGB565 byte order is
big-endian: little-endian made a window score 83, big-endian 46, and a person in frame was boxed both ways.

## Open points

- Setup mode with the D1 switch and the settings/log page are built but not yet bench-tested.
- Detection quality is untested outdoors, at night, and against runners; the threshold needs tuning on real footage.
- The UART path to the trigger board and running on the boost/battery are untested (waiting on the breadboards).
- The ESP32 ROM prints boot text on the same UART at power-up; the trigger board ignores anything not starting `EW,`.
