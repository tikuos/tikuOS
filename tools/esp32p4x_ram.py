#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4x_ram.py - Load the Function EV Board panel diagnostic into SRAM.
#
# The ESP32-P4 v3.2 ROM and image revision must match. Each USB query
# checks the native CPU0 tick and reads the GT911 product ID over I2C.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path
import sys
import time

import esp32p4_ram as ram


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--serial")
    options = parser.add_mutually_exclusive_group()
    options.add_argument("--read-panel", action="store_true",
                         help="read back a programmed EK79007 gamma register")
    options.add_argument("--frame-test", action="store_true",
                        help="check RGB565 scanout from PSRAM for DMA errors and underrun")
    options.add_argument("--worker-test", action="store_true",
                         help="run a kernel worker on its PSRAM stack across owner calls")
    options.add_argument("--display-test", action="store_true",
                         help="exercise kernel display drawing and paired submission")
    options.add_argument("--flip-test", action="store_true",
                         help="alternate two PSRAM frames at DMA boundaries")
    options.add_argument("--observe-frame", action="store_true",
                         help="display a bordered PSRAM frame for 2700 refresh intervals")
    options.add_argument("--observe-seconds", type=int, choices=range(0, 301), metavar="0..300",
                        default=0, help="hold the verified RAM test image for observation")
    args = parser.parse_args()
    image = args.image.read_bytes()
    meta = ram.validate_image(image, revision=302)
    ram.validate_elf(args.elf.read_bytes(), meta)
    if sum(block.count(b"TikuOS ESP32-P4X RAM panel diagnostic") for _, block in meta["segments"]) != 1:
        raise ValueError("expected the P4X panel diagnostic")
    if args.check_only:
        print("PASS P4X v3.2 image, ELF and diagnostic SRAM placement")
        return
    import esptool
    import serial
    from serial.tools import list_ports

    ram.require_port(args.port, args.serial, list(list_ports.comports()))
    with ram.usb_console(serial, args.port) as wire:
        esp = ram.connect_rom(esptool, wire)
        ram.validate_target(esp, revision=302, eco=7)
        try:
            esptool.load_ram(esp, image)
            wire.timeout = 0.1
            deadline = time.monotonic() + 6
            next_query = time.monotonic() + 0.3
            response = bytearray()
            while time.monotonic() < deadline:
                if time.monotonic() >= next_query:
                    try:
                        wire.write(b"i")
                    except serial.SerialTimeoutException:
                        pass
                    next_query = time.monotonic() + 1
                response.extend(wire.read(4096))
                if b"FAIL" in response or response.count(b"PASS GT911 poll") >= 2:
                    break
            print(response.decode(errors="replace"), end="")
            if b"FAIL" in response or any(response.count(marker) < 2 for marker in
                    (b"TikuOS ESP32-P4X RAM panel diagnostic", b"PASS CPU0 timer", b"PASS DSI host pattern configured",
                     b"PASS mapped PSRAM 32 MiB", b"PASS GT911 product ID", b"PASS GT911 poll")):
                raise ValueError("incomplete panel diagnostic")
            request = None
            if args.frame_test or args.flip_test or args.observe_frame:
                command = b"d" if args.flip_test else b"f"
                if args.observe_frame:
                    print("Displaying bordered framebuffer for 2700 refresh intervals", flush=True)
                    command = b"e"
                request = command, b"PASS framebuffer DMA and VSYNC"
            elif args.worker_test:
                request = b"w", b"PASS external-stack kernel worker"
            elif args.display_test:
                request = b"a", b"PASS kernel display flips"
            elif args.read_panel:
                request = b"p", b"PASS panel gamma readback"
            if request:
                command, marker = request
                wire.write(command)
                deadline = time.monotonic() + (100 if args.observe_frame else 6)
                frame = bytearray()
                while time.monotonic() < deadline and b"\n" not in frame:
                    frame.extend(wire.read(256))
                print(frame.decode(errors="replace"), end="")
                if marker not in frame:
                    raise ValueError("panel qualification did not pass")
            if args.observe_seconds:
                print(f"Holding color bars for {args.observe_seconds} seconds", flush=True)
                time.sleep(args.observe_seconds)
        finally:
            esp.hard_reset()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
