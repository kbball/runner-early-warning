#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kevin Ball
"""Timestamped capture of a XIAO's USB serial log that survives the port disappearing and coming back.

    python3 tools/serial_capture.py 30                      # first usbmodem port, 30 seconds
    python3 tools/serial_capture.py 60 --serial 3D918EB27F98BC99   # a specific board, by USB serial number

Needs pyserial (it ships in PlatformIO's Python: ~/.platformio/penv/bin/python). Reconnects when a board resets
or re-enumerates, which is why it is used instead of `pio device monitor` for short bench checks. Do not run it
while uploading firmware: an open port stops PlatformIO's reset into the bootloader.

The nRF boards wait about 1.5 s for USB after boot, so start this just before (or right after) a reset to catch the
header lines.
"""
import argparse
import glob
import sys
import time

import serial
from serial.tools import list_ports


def find_port(serial_number):
    if serial_number:
        for p in list_ports.comports():
            if p.serial_number == serial_number:
                return p.device
        return None
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("seconds", type=float)
    ap.add_argument("--serial", help="USB serial number of the board (see `pio device list`)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    t0 = time.time()
    deadline = t0 + args.seconds
    port = None
    ser = None
    buf = b""
    last = None
    while time.time() < deadline:
        if ser is None:
            dev = find_port(args.serial)
            if not dev:
                time.sleep(0.05)
                continue
            try:
                ser = serial.Serial(dev, args.baud, timeout=0.2)
            except Exception:
                ser = None
                time.sleep(0.05)
                continue
            if dev != last:
                print(f"[{time.time() - t0:6.2f}s] # opened {dev}", flush=True)
                last = dev
        try:
            data = ser.read(256)
        except Exception:
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            continue
        if data:
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                print(f"[{time.time() - t0:6.2f}s] {line.decode(errors='replace').rstrip()}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
