# Runner Early-Warning Node — custom firmware

Firmware for the XIAO ESP32S3 Sense camera board in the runner early-warning node
(PIR wake -> on-device person detection -> verdict over UART to the RAK4631 running MeshCore).

Design, decisions and test plan: `~/code/brain/notes/ares/2026-10/runner-early-warning-node.md`, superseded in part by
`~/code/brain/notes/ares/2026-10/runner-early-warning-two-node.md` (two nodes: a PIR node and a camera node).

## Stages

1. Baseline flash: `CameraWebServer` on `seeed_xiao_esp32s3` (OPI PSRAM).
2. Timing/logging firmware: PIR on D0 (GPIO1), deep-sleep wake, ms timestamps, SD frame capture.
3. Person detection (ESP-DL pedestrian detect or TFLite Micro), with trail-strip crop.
4. UART verdict out on D6/D7 with a fixed message prefix.

Toolchain: PlatformIO (`pio`), Arduino framework.

## Layout (two-node redesign, 2026-10-12)

The design moved to a PIR node plus a camera node. The stages above describe the camera board; the repo is
now organised per target:

- `pir-node/` XIAO nRF52840 + PIR: trigger and heartbeat beacons (flashed; heartbeats verified; PIR not attached yet).
- `trigger-node/` XIAO nRF52840 on the camera node: BLE scanner, drives the boost EN, reads the camera's UART,
  logs everything over USB serial (flashed; receives the PIR node's heartbeats; EN and camera UART untested).
- `camera-node/` XIAO ESP32-S3 Sense camera: boots on power-up, sends the `EW,...` lines, grabs frames, stub detector
  (written and compiles; not run on hardware yet).
- `common/` shared code with host unit tests (`cd common && pio test -e native`): `TriggerPacket` (the BLE packet),
  `CameraMsg` (the camera's UART lines), `TriggerLogic` (packet-group tracking and the enable window), `MeshCoreFrames` (companion-protocol frames), plus the
  XIAO nRF52840 board definition used by both nRF projects.
- `hardware/` KiCad project and schematic generator for the carrier board.
- `docs/pages/` the breadboard layout page (with its generator) and the carrier concept render.
- `tools/serial_capture.py` timestamped serial capture that survives a board resetting; used for bench checks.
- The MeshCore board runs the send-only runner alert firmware, which lives in the MeshCore fork
  (`kbball/MeshCore-tracker`, branch `feature/runner-alert`), not in this repository.
- Still to write: a real person-detection model for the camera (stage 3 above).

## License

GPL-3.0-or-later; see `LICENSE`. Third-party code, the licenses of the toolchains this builds against, and why they do
not conflict are recorded in `NOTICE.md`. Copyright (C) 2026 Kevin Ball.
