#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32c5_flash.py - guarded C5 boot-image installation without storage erasure.
# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path
import sys

import esp32c5_ram as ram
import esp32c5_xip as xip_image

BOOT_OFFSET = 0x2000
BOOT_LIMIT = 0x100000
FLASH_BYTES = 0x400000


def validate_flash_target(esp, meta):
    """Refuse another ROM, silicon revision, flash geometry or security profile."""
    if esp.CHIP_NAME != "ESP32-C5" or esp.IS_STUB:
        raise ValueError("requires an ESP32-C5 ROM loader")
    if esp.BOOTLOADER_FLASH_OFFSET != BOOT_OFFSET:
        raise ValueError("unexpected C5 ROM boot offset")
    if esp.secure_download_mode:
        raise ValueError("secure download mode is not supported")
    ram.validate_identity(esp.read_reg(0x40000010), esp.read_reg(0x40000014),
                          esp.get_major_chip_version() * 100 + esp.get_minor_chip_version(), meta)
    security = esp.get_security_info()
    if (security["chip_id"] != ram.CHIP_ID or
            security["flags"] & 5 or security["flash_crypt_cnt"] != 0):
        raise ValueError("secure boot/encryption or unexpected chip identity: refusing flash")
    esp.flash_spi_attach(0)
    identity = esp.flash_id()
    if identity in (0, 0xFFFFFF) or identity >> 16 != 22:
        raise ValueError("C5 flash layout requires a 4 MiB flash")


def install(esp, data, commands, xip=None):
    """Validate the pair, write/verify XIP before boot, then request USB reset."""
    meta = ram.validate_image(data, "kernel")
    xip_image.validate_pair(data, xip)
    if BOOT_OFFSET + ((len(data) + 4095) & ~4095) > BOOT_LIMIT:
        raise ValueError("boot image overlaps the XIP partition")
    validate_flash_target(esp, meta)
    if xip is not None:
        companion = [(xip_image.XIP_OFFSET, xip)]
        commands.write_flash(esp, companion, flash_size="keep", flash_mode="keep",
                             flash_freq="keep", erase_all=False, force=False,
                             encrypt=False, no_compress=True)
        commands.verify_flash(esp, companion)
    entries = [(BOOT_OFFSET, data)]
    commands.write_flash(esp, entries, flash_size="keep", flash_mode="keep",
                         flash_freq="keep", erase_all=False, force=False,
                         encrypt=False, no_compress=True)
    commands.verify_flash(esp, entries)
    esp.hard_reset()


def main():
    """An explicit --port authorizes replacing the C5 boot image, not /data."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--xip", type=Path, help="matching companion for an XIP build")
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--no-monitor", action="store_true",
                        help="verify and reset without requiring a shell in the image")
    args = parser.parse_args()
    try:
        import esptool
        from esptool import cmds
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("this loader requires esptool==5.1.0")
        with args.image.open("rb") as stream:
            data = stream.read(384 * 1024 + 1)
        ram.validate_image(data, "kernel")
        xip = None
        if args.xip:
            with args.xip.open("rb") as stream:
                xip = stream.read(xip_image.XIP_CAPACITY + 1)
        xip_image.validate_pair(data, xip)
        # Open the log before any board write, so an existing log refuses safely.
        log = args.log.open("xb") if args.log else None
        try:
            ram.require_idle_usb_port(args.port, list_ports.comports())
            with ram.usb_console(serial, args.port) as wire:
                esp = esptool.detect_chip(wire, connect_attempts=3)
                install(esp, data, cmds, xip)
                if not args.no_monitor:
                    ram.capture_kernel(wire, 8, log)
        finally:
            if log:
                log.close()
        status = "reset requested" if args.no_monitor else "shell clock advancing"
        print(f"PASS: C5 boot image verified; {status}; storage partitions were not erased")
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"C5 flash: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
