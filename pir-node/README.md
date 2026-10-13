# PIR node firmware

XIAO nRF52840 + PIR + 18650. No LoRa, no MeshCore. It tells the camera node's trigger board that
someone is coming, over a dedicated BLE advertising link.

Design: `~/code/brain/notes/ares/2026-10/runner-early-warning-two-node.md` (sections 2, 10, 11).
Wiring for the bench rig: the breadboard layout page (PIR node section).

## Behaviour

- PIR output on D0, interrupt on the rising edge. The CPU sleeps in the RTOS idle task between events.
- **Trigger:** on a PIR edge, advertise a burst of `Trigger` packets for `TRIGGER_BURST_MS` (1.5 s).
- **Heartbeat:** every `HEARTBEAT_MS` (5 s), advertise a short burst of `Heartbeat` packets. This measures
  the link without anyone walking past and shows the node is alive.
- Edges are ignored for `PIR_WARMUP_MS` after boot (AM312 settling, value unverified) and for
  `PIR_COOLDOWN_MS` after a trigger group.
- Packets are non-connectable BLE advertising with manufacturer data. Format and tests are in
  `../common/lib/TriggerPacket`. Every packet in a group carries the same sequence number, so the
  receiver can dedupe a burst and count losses from gaps.

## Serial log (USB, 115200)

One CSV line per event: `ms,event,seq,vbat_mv`

```
# ew-pir-node v0  columns: ms,event,seq,vbat_mv
5000,HEARTBEAT,0,4011
12345,TRIGGER,1,4010
12900,IGNORED_COOLDOWN,,
```

## Build and flash

```
pio run                 # build
pio run -t upload --upload-port /dev/cu.usbmodemXXXX   # flash over USB
pio device monitor
```

**Flash from the bootloader.** Double-tap reset first; the board then shows a `XIAO-SENSE` (or `XIAO-BOOT`)
drive and a serial port. The automatic 1200-baud "touch" that PlatformIO uses to reset a running app into the
bootloader failed twice on this Mac and left the board with no valid app. It then sat in the bootloader, which
looks like a dead board but is not: replug or double-tap and flash again. Don't keep a serial monitor open on
the port while uploading. Catch the boot header by connecting within about 1.5 s of boot (the firmware waits
that long for USB), or just read the heartbeats.

Build flags (see `platformio.ini`): `LOG_SERIAL` and `LED_ON_TRIGGER` should be 0 for current
measurements, since USB and the LED both add draw. `TX_POWER_DBM`, `HEARTBEAT_MS` and the burst
lengths are test variables.

Unit tests for the shared packet code run on the host: `cd ../common && pio test -e native`.

## Status

Compiles (RAM 6.5%, flash 15.4%). **Flashed and running on a board (2026-10-12), PIR not attached.**
Boot log is clean: the header and config lines print, then a `HEARTBEAT` every 5000 ms exactly (uptime
5703, 10702, 15702, 20702 ms) with the sequence counting up from 0, and no spurious trigger or ignored-edge
lines with D0 floating. Not yet checked: a real trigger, and anything over the air.

The board is a XIAO nRF52840 **Sense** (bootloader: UF2 0.6.1, SoftDevice S140 7.3.0, Seeed_XIAO_nRF52840_Sense).
The S140 7.3.0 match confirms the linker script start address (0x27000). The Sense variant has an IMU and a
mic that the plain board lacks; check they are off, since they would add to the sleep current.

Known gaps:

- Battery voltage uses the XIAO divider (`VBAT_DIVIDER_X1000`, 2953 from the board's 1M + 512k
  resistors). With no battery on the pads it reads garbage (356-994 mV), as expected. It needs calibrating
  against a meter with the 18650 on the BAT pads.
- 1 Mbps legacy advertising only. Coded PHY (longer range) is a later step if the link test needs it.
- Sleep current, the real PIR warm-up time, and range at 20 m are all unmeasured (test plan stages 1 and 2).

## Board support

PlatformIO has no XIAO nRF52840 board or pin map, so `../common/xiao_nrf52840/` (shared with the trigger board)
holds copies from the MeshCore repo (MIT); see `../common/xiao_nrf52840/NOTICE`. No MeshCore firmware code is used.
The linker script is the Adafruit core's v6 script with the flash start moved for SoftDevice 7.3.0.
