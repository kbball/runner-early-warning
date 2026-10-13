# Camera node camera firmware

XIAO ESP32-S3 Sense. It is powered only while the trigger board holds the boost EN high, so every run starts
from a cold boot. On boot it reports on its UART, grabs frames for a session, reports a verdict, and sleeps.

Design: `~/code/brain/notes/ares/2026-10/runner-early-warning-two-node.md` (sections 2, 10, 11, 12).

## Behaviour

1. `setup()` opens the hardware UART (D7 RX, D6 TX, GPIO44/43, 115200) and sends `EW,BOOT,<ms>` **first**, so the
   trigger board sees the camera come up.
2. Initialises the camera (JPEG, QVGA, two frame buffers in PSRAM) and logs how long that took.
3. Grabs frames for `SESSION_MS` (30 s), sending `EW,FRAME,<ms>,<n>` for the first 5 frames and every 10th after.
4. Scores each frame with the detector. A `PERSON` verdict needs `PERSON_CONFIRM_FRAMES` (2) consecutive frames at
   or above `PERSON_THRESHOLD` (60) and ends the session early.
5. Sends `EW,VERDICT,<ms>,PERSON|NONE,<best score>` then `EW,DONE,<ms>`, waits 3 s for the boost to drop, then deep
   sleeps (no wake source, so it stays down until power returns).

Line formats are produced by `../common/lib/CameraMsg` (the same code the trigger board parses with) and unit
tested on the host: `cd ../common && pio test -e native`.

## The detector is a stub

There is no person-detection model yet. `detectPerson()` returns 0, so the verdict is `NONE` unless built with
`-D STUB_FORCE_PERSON=1`, which reports a person after `STUB_PERSON_AFTER_FRAMES` frames. That is enough to measure
boot and frame timing and to test the whole chain (PIR node, trigger board, boost, camera) before the model exists.
Replace `detectPerson()` with ESP-DL or TFLite Micro later (notes, stage 3).

## Optional pieces

- `SAVE_FRAMES=1` saves JPEGs to the microSD card under `/ew/<boot>_<n>.jpg` (for tuning and the overnight false-trigger
  soak). The SD chip-select is `SD_CS_PIN` (21); the notes flag it as unverified (GPIO3 vs GPIO21).
- `MESHCORE_FRAMES=1` also sends MeshCore companion frames (App Start, then Send Channel Message on channel
  `MESH_CHANNEL_IDX`) on a confirmed person, on the same UART TX line. Off by default: the MeshCore nRF needs a custom
  build with its UART enabled first (two-node notes, section 12). The trigger board ignores the binary bytes. The
  timestamp is `MESH_EPOCH_BASE` (2026-10-12) plus uptime, because the camera has no clock; how MeshCore treats that is
  unverified.

## Wiring (matches the breadboard page)

| Camera pin | Goes to |
|---|---|
| D6 (TX, GPIO43) | trigger nRF D7 (RX) |
| 5V | the 5 V rail from the MiniBoost, through the Schottky |
| GND | common GND |

The camera's own USB-C is for flashing and the debug log only. With the boost off the rail, the board powers from USB.

## Build and flash

```
pio run
pio run -t upload       # put the board in its bootloader first (hold B/BOOT while plugging in) if upload fails
pio device monitor      # debug log on USB serial; the EW,... lines are on D6, not here
```

## Status

Compiles (RAM 7.3%, flash 10.4%). **Not yet run on hardware.** Open points:

- Camera pin map is the Seeed XIAO ESP32-S3 Sense one, from memory of their example; check it on the first run (the
  debug log reports whether `esp_camera_init` succeeded).
- Boot-to-first-frame time is unmeasured; the debug log prints it, and the trigger board's log shows the UART view of it.
- `Serial0` is the hardware UART only because the build sets `ARDUINO_USB_CDC_ON_BOOT=1`; confirm D6 carries the lines.
- The ESP32 ROM prints boot text on the same UART at power-up; the trigger board ignores anything not starting `EW,`.
