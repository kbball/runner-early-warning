# Licensing and third-party notices

This project is licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`); the full text is
in `LICENSE`. Copyright (C) 2026 Kevin Ball. First-party source files carry an SPDX header.

This file records every other piece of code or data that the repository contains or builds against, its license, and
why it does not conflict with GPL-3.0-or-later. It was checked on 2026-10-12 against the license files and headers
in the installed toolchains. It is a good-faith record, not legal advice.

## Included in this repository

| What | Where | License | Notes |
|---|---|---|---|
| XIAO nRF52840 board definition and pin map | `common/xiao_nrf52840/boards/*.json`, `common/xiao_nrf52840/variants/` | MIT, Copyright (c) 2025 Scott Powell / rippleradios.com (the MeshCore repository, commit 0679dbef) | MIT is GPL-compatible. The notice is kept in `common/xiao_nrf52840/NOTICE`. The copies carry no header of their own; the pin map originates in the Seeed XIAO nRF52840 variant, whose files are normally LGPL-2.1-or-later (unverified), which is also compatible. No MeshCore firmware code is used |
| Linker script | `common/xiao_nrf52840/boards/nrf52840_s140_v7.ld` | modified copy of `nrf52840_s140_v6.ld` from the Adafruit nRF52 Arduino core (flash start changed to 0x27000 for SoftDevice 7.3.0) | The core is LGPL-2.1-or-later with MIT parts (see below); compatible |
| KiCad symbol definitions embedded in the schematic | `hardware/ew-carrier/ew-carrier.kicad_sch` | KiCad libraries: CC-BY-SA-4.0 **with the KiCad design exception** | The exception waives the share-alike clause for designs and generated files that use the library data. The libraries themselves are not redistributed here |

## Build-time dependencies (not stored in the repository)

PlatformIO downloads these when you build. They are linked into the firmware you build, so their terms apply if you
distribute that binary.

| What | License | Used by | Compatibility with GPL-3.0-or-later |
|---|---|---|---|
| Adafruit nRF52 Arduino core (`cores/nRF5`) | LGPL-2.1-or-later (Arduino LLC, Sandeep Mistry, Adafruit), some files MIT or BSD | `pir-node`, `trigger-node` | LGPL-2.1 "or any later version" can be combined with GPL-3.0 |
| Adafruit Bluefruit52Lib | MIT | `pir-node`, `trigger-node` | compatible |
| Nordic S140 SoftDevice 7.3.0 and its API headers | Nordic Semiconductor proprietary license | `pir-node`, `trigger-node` | **Not part of this repository and not redistributed.** The SoftDevice already sits on the board with the Seeed bootloader; the firmware links only against its API (SVC calls). If you ever publish prebuilt firmware or a full flash image, read Nordic's license terms first. The DFU packages built here contain only the application |
| ESP-IDF, esp32-camera, ESP-DL and the `pedestrian_detect` component with its PICO_S8_V1 model (Espressif) | Apache-2.0 (the components' `LICENSE` files; ESP-IDF's third-party parts carry their own, mostly permissive, licenses) | `camera-node` | Apache-2.0 is compatible with GPL-3.0 (it is not compatible with GPL-2.0-only, which is why this project is GPL-3.0 or later). The model file is fetched by the component manager at build time and flashed to a partition; it is not stored in this repository. Pulled in by `camera-node/src/idf_component.yml`; the exact versions are in `camera-node/dependencies.lock` |
| Unity test framework | MIT | host tests in `common/` | compatible |

## Separate repositories

- The MeshCore firmware fork `kbball/MeshCore-tracker` holds the runner alert firmware (branch `feature/runner-alert`).
  It is a fork of MeshCore (MIT) and stays MIT. Its `CameraLine.h` is a copy of the parsing half of
  `common/lib/CameraMsg` from this repository. The copyright holder of both is the same, so the copy in the fork is
  offered under the fork's MIT license; this repository's copy remains GPL-3.0-or-later.

## Hardware files

`hardware/` is licensed under the same GPL-3.0-or-later as the rest, as requested. A hardware-specific license such as
CERN-OHL-S-2.0 is a better fit for schematics and boards and could be substituted for `hardware/` later.
