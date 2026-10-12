# Trigger board firmware (camera node)

The always-on XIAO nRF52840 on the camera node. No LoRa, no MeshCore (that is a separate board).
It listens for the PIR node, powers the camera, and logs everything to USB serial.

Design: `~/code/brain/notes/ares/2026-10/runner-early-warning-two-node.md` (sections 10 and 11).
Wiring for the bench rig: the breadboard layout page (camera node section).

## Behaviour

1. Scans BLE for the PIR node's manufacturer-data packets (`../common/lib/TriggerPacket`).
2. Packets that share a sequence number are one **group** (a burst). The first packet of a new group is
   logged straight away; a summary line follows when the group ends. Gaps in the sequence numbers show
   as missed groups.
3. A new **Trigger** group raises `EN_PIN` (D6, the MiniBoost EN) *before* anything is logged.
4. While EN is high it reads the camera's UART (D7) and logs every `EW,...` line
   (`../common/lib/CameraMsg`). Boot text from the ESP32's ROM is ignored.
5. EN drops after `CAMERA_WINDOW_MS` (45 s), or `DONE_GRACE_MS` (0.5 s) after the camera prints
   `EW,DONE`. Further triggers extend the window, capped at `CAMERA_MAX_MS` (90 s).

The grouping and window rules are in `../common/lib/TriggerLogic` and are unit tested on the host
(`cd ../common && pio test -e native`).

## Wiring (matches the breadboard page)

| Trigger nRF pin | Goes to |
|---|---|
| D6 | MiniBoost EN |
| D7 | camera D6 (TX), the UART line |
| GND | common GND |
| D5 | **leave unwired.** Serial1 is given D5 as a dummy TX so it does not drive D6 (the EN line). |
| USB | the computer: power and the log |

## Serial log (USB, 115200)

One CSV line per event, time in this board's `millis()`:

```
# ew-trigger-node v0  RX: ms,RX,type,seq,rssi,sender_uptime_ms,vbat_mv,gap,reset
12345,EN,HIGH,trigger              <- boost enabled
12346,RX,TRIGGER,17,-71,98765,4010,0,0
13950,GROUP,17,62,-68              <- 62 packets heard in that burst, best RSSI -68
14100,CAM,EW,BOOT,812
15400,CAM,EW,VERDICT,2100,PERSON,87
15500,CAM,EW,DONE,2300
16000,EN,LOW,done
```

- `RX` columns: `gap` is groups missed since the previous group, `reset` is 1 if the sender's sequence went
  backwards or jumped (it probably rebooted).
- `GROUP` packet counts give a link-quality number per burst even when nothing was missed.
- Set `LOG_ALL_PACKETS=1` to log every packet, `LOG_UART_NOISE=1` to see the ESP32's boot text.

## Camera protocol

The camera prints these on its UART (see `CameraMsg.h`); anything else is ignored:

```
EW,BOOT,<ms>
EW,FRAME,<ms>,<n>
EW,VERDICT,<ms>,PERSON|NONE,<score 0..100>
EW,DONE,<ms>
```

## Build and flash

```
pio run
pio run -t upload       # double-tap reset first if the board is not in its bootloader
pio device monitor
```

## Status

Compiles (RAM 6.7%, flash 15.7%) and the logic is unit tested. **Not yet run on hardware.** Open points:

- The scan duty (`SCAN_WINDOW_MS` / `SCAN_INTERVAL_MS`, default 50/100) is a guess. It trades current
  against the chance of missing a heartbeat; tune it against the always-on current budget.
- Before `setup()` runs, the EN pin floats. Check whether the MiniBoost's EN has its own pull resistor, and
  add a pulldown if the camera could power up briefly at boot.
- `Serial1.setPins(...)` with a dummy TX pin was written from the Adafruit core's API and compiles, but
  needs a check on hardware that D6 stays quiet.
- The camera firmware that prints the `EW,...` lines does not exist yet.
- The wake-on-trigger latency (packet received to EN high) is logged only as two timestamps; measure it
  with a logic analyzer as the test plan says.
