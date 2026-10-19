#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_board_flash.py - verified P4 firmware installation on the FireBeetle 2.
#
# For a P4 board whose original image is not kept: checks the ROM, silicon,
# security and flash, writes and verifies the code pair, boots it from flash
# and waits for the shell, leaving storage unwritten; --erase-chip erases all.
#
# SPDX-License-Identifier: Apache-2.0
import argparse
import io
from pathlib import Path
import sys

import esp32p4_firmware as firmware
import esp32p4_flash as flash
import esp32p4_ram as ram
import esp32p4_xip as pairing
from esp_usb_console import require_idle_usb_port, usb_console

BOARD = b"firebeetle2_esp32p4"
PARTITION_TABLE = 0x8000
PARTITION_MAGIC = b"\xaa\x50"


def require_board_image(boot):
    """Refuse an image built for another board: only the FireBeetle's own."""
    if BOARD not in boot:
        raise ValueError("image was not built with BOARD=firebeetle2_esp32p4")


def require_no_vendor_layout(esp):
    """Refuse an ESP-IDF partition table: its storage would read as foreign.

    The ROM loader cannot read flash, so the stub reads the first entry.
    """
    stub = esp.run_stub()
    if stub.read_flash(PARTITION_TABLE, 2) == PARTITION_MAGIC:
        raise ValueError("flash holds an ESP-IDF partition table (vendor "
                         "firmware); erase the chip first (make erase)")
    return stub


def install(esp, commands, boot, xip, log):
    """Write and verify XIP before boot, then boot from flash.

    The ROM loader shows the strap of its USB download entry, not the BOOT
    pin, so the caller's shell prompt is what proves the flash boot.
    """
    ranges = firmware.extents(boot, xip)
    esp = require_no_vendor_layout(esp)
    for payload, (start, end) in ((xip, ranges[1]), (boot, ranges[0])):
        flash.program(esp, commands, start, payload + b"\xff" * (end - start - len(payload)))
    flash.record(log, "PASS: code pair written and verified; storage not written")
    flash.boot_flash(esp, require_strap=False)


def main():
    """An explicit --port authorizes replacing the code pair, or with
    --erase-chip the whole flash."""
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("image", "elf", "xip"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", help="USB serial number the port must report")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--erase-chip", action="store_true",
                        help="erase the whole flash instead of installing")
    args = parser.parse_args()
    try:
        import esptool
        from esptool import cmds
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        if not args.erase_chip:
            if not (args.image and args.elf and args.xip):
                raise ValueError("--image, --elf and --xip are required to install")
            boot = firmware.read_bounded(args.image, ram.KERNEL_LOW_END - ram.RAM_START + 256)
            xip = firmware.read_bounded(args.xip, pairing.XIP_CAPACITY)
            elf = firmware.read_bounded(args.elf, 4 * 1024 * 1024)
            pairing.validate_elf(elf, boot, xip, firmware=True)
            require_board_image(boot)
        ports = list(list_ports.comports())
        if args.serial:
            ram.require_port(args.port, args.serial, ports)
        else:
            require_idle_usb_port(args.port, ports)
        # Open the log before any board write, so an existing log refuses safely.
        log = args.log.open("xb") if args.log else io.BytesIO()
        try:
            with usb_console(serial, args.port) as wire:
                esp = flash.connect(esptool, wire)
                if args.erase_chip:
                    stub = esp.run_stub()
                    cmds.erase_flash(stub)
                    stub.hard_reset()
                    print("PASS: P4 checks passed; whole flash erased")
                    return 0
                install(esp, cmds, boot, xip, log)
                firmware.capture_prompt(wire, log)
        finally:
            log.close()
        print("PASS: P4 code pair verified and booted to the shell; storage not written")
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"P4 flash: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
