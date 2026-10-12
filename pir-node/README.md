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
pio run -t upload       # flash over USB (double-tap reset first if the board is not in its bootloader)
pio device monitor
```

Build flags (see `platformio.ini`): `LOG_SERIAL` and `LED_ON_TRIGGER` should be 0 for current
measurements, since USB and the LED both add draw. `TX_POWER_DBM`, `HEARTBEAT_MS` and the burst
lengths are test variables.

Unit tests for the shared packet code run on the host: `cd ../common && pio test -e native`.

## Status

Compiles (RAM 6.5%, flash 15.4%). **Not yet run on hardware.** Known gaps:

- Battery voltage uses the XIAO divider (`VBAT_DIVIDER_X1000`, 2953 from the board's 1M + 512k
  resistors). It needs calibrating against a meter and is only valid with the 18650 on the BAT pads.
- 1 Mbps legacy advertising only. Coded PHY (longer range) is a later step if the link test needs it.
- Sleep current, the real PIR warm-up time, and range at 20 m are all unmeasured (test plan stages 1 and 2).
- The XIAO nRF52840 bootloader ships with SoftDevice S140 7.3.0, which is why the linker script starts
  flash at 0x27000. Confirm against the actual board on first flash.

## Board support

PlatformIO has no XIAO nRF52840 board or pin map, so `boards/` and `variants/xiao_nrf52/` are copied from
the MeshCore repo (MIT); see `variants/xiao_nrf52/NOTICE`. No MeshCore firmware code is used.
The linker script is the Adafruit core's v6 script with the flash start moved for SoftDevice 7.3.0.
