# Runner Early-Warning Node — custom firmware

Firmware for the XIAO ESP32S3 Sense camera board in the runner early-warning node
(PIR wake -> on-device person detection -> verdict over UART to the RAK4631 running MeshCore).

Design, decisions and test plan: `~/code/brain/notes/ares/2026-10/runner-early-warning-node.md`.

## Stages

1. Baseline flash: `CameraWebServer` on `seeed_xiao_esp32s3` (OPI PSRAM).
2. Timing/logging firmware: PIR on D0 (GPIO1), deep-sleep wake, ms timestamps, SD frame capture.
3. Person detection (ESP-DL pedestrian detect or TFLite Micro), with trail-strip crop.
4. UART verdict out on D6/D7 with a fixed message prefix.

Toolchain: PlatformIO (`pio`), Arduino framework.
