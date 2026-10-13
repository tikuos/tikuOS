#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32p4_firmware.py - Backup-checked native firmware installation on one P4.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
from pathlib import Path
import sys
import time

import esp32p4_flash as flash
import esp32p4_ram as ram
import esp32p4_storage_test as storage
import esp32p4_xip as pairing
from esp32p4_layout import LAYOUT
from esp_usb_console import usb_console


def read_bounded(path, limit):
    """Reject oversized input without reading unbounded files into memory."""
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"input exceeds size bound: {path}")
    return data


def extents(boot, xip):
    """List every sector firmware installation or normal operation can modify."""
    return [(LAYOUT["BOOT"][0], LAYOUT["BOOT"][0] + ((len(boot) + 4095) & ~4095)),
            (LAYOUT["XIP"][0], LAYOUT["XIP"][0] + ((len(xip) + 4095) & ~4095)),
            (LAYOUT["REGION"][0], LAYOUT["MIRROR_B"][1])]


def check_outside(esp, backup, ranges):
    """Refuse recovery if bytes outside the authorized reservations changed."""
    previous = 0
    for start, end in ranges:
        if start < previous or start % 4096 or end % 4096 or end <= start:
            raise ValueError("invalid firmware write extent")
        if start > previous:
            flash.check(esp, previous, backup[previous:start])
        previous = end
    if previous < len(backup):
        flash.check(esp, previous, backup[previous:])


def changed_sectors(esp, backup, start, end):
    """Find modified sectors using ROM digests; unchanged subranges are skipped."""
    if esp.flash_md5sum(start, end - start).lower() == flash.digest(backup[start:end]):
        return []
    if end - start == 4096:
        return [start]
    middle = start + ((end - start) // 8192) * 4096
    return (changed_sectors(esp, backup, start, middle) +
            changed_sectors(esp, backup, middle, end))


def restore(esp, commands, backup, ranges, log):
    """Restore only changed sectors of the prevalidated original backup."""
    check_outside(esp, backup, ranges)
    sectors = []
    for start, end in ranges:
        sectors.extend(changed_sectors(esp, backup, start, end))
    flash.record(log, f"RESTORE: {len(sectors)} changed sectors; outside reservations unchanged")
    for start in reversed(sectors):
        entries = [(start, backup[start:start + 4096])]
        commands.write_flash(esp, entries, flash_size="keep", flash_mode="keep",
                             flash_freq="keep", erase_all=False, force=False,
                             encrypt=False, no_compress=True)
        flash.check(esp, start, backup[start:start + 4096])
    flash.check(esp, 0, backup)
    flash.record(log, "PASS: original full 16 MiB flash restored exactly")


def capture_prompt(wire, log, seconds=30):
    """Require a live shell prompt; preserve complete boot and failure evidence."""
    data = bytearray()
    deadline = time.monotonic() + seconds
    sent = False
    while time.monotonic() < deadline:
        chunk = wire.read(4096)
        if chunk:
            data.extend(chunk)
            log.write(chunk)
            log.flush()
        if any(word in data for word in (b"FATAL:", b"TRAP ", b"waiting for download")):
            raise ValueError("firmware boot fault or ROM download mode")
        if len(data) > 65536:
            raise ValueError("boot output exceeds bound")
        if b"tikuOS:/> " in data:
            return bytes(data)
        if not sent and time.monotonic() > deadline - seconds + 3:
            wire.write(b"\r")
            sent = True
    raise ValueError("firmware did not reach its shell prompt")


def main():
    """Install explicitly backed-up code; boot may provision only blank storage."""
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("image", "elf", "xip"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--first-install", action="store_true")
    parser.add_argument("--ram-run", action="store_true",
                        help="install the pair, then start via ROM RAM load; does not qualify flash boot")
    parser.add_argument("--restore-original", action="store_true")
    parser.add_argument("--resume", action="store_true",
                        help="verify the installed pair, then RAM-load it without writing flash")
    for name in ("backup", "log", "recovery-image", "recovery-elf"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--port")
    parser.add_argument("--serial")
    args = parser.parse_args()
    try:
        boot = read_bounded(args.image, ram.KERNEL_LOW_END - ram.RAM_START + 256)
        xip = read_bounded(args.xip, pairing.XIP_CAPACITY)
        elf = read_bounded(args.elf, 4 * 1024 * 1024)
        layout = pairing.validate_elf(elf, boot, xip, firmware=True)
        ranges = extents(boot, xip)
        if ranges[0][1] > LAYOUT["BOOT"][1] or ranges[1][1] > LAYOUT["XIP"][1]:
            raise ValueError("code reservation exceeded")
        print(f"P4 firmware validated: {layout}; code/storage extents={ranges}")
        if args.check_only:
            return 0
        if sum((args.first_install, args.restore_original, args.resume)) != 1 or not all((args.backup, args.log,
                args.recovery_image, args.recovery_elf)):
            raise ValueError("requires first-install, restore-original or resume, backup, new log and recovery pair")
        if args.ram_run and not args.first_install:
            raise ValueError("--ram-run requires --first-install")
        backup = read_bounded(args.backup, ram.FLASH_SIZE)
        storage.reference(backup)
        recovery = read_bounded(args.recovery_image, ram.KERNEL_LOW_END - ram.RAM_START + 256)
        meta = ram.validate_image(recovery, True)
        if ram.has_firmware(meta) or ram.has_storage_writer(meta) or ram.has_scratch_writer(meta):
            raise ValueError("recovery image must not write storage")
        pairing.validate_pair(recovery)
        ram.validate_elf(read_bounded(args.recovery_elf, 4 * 1024 * 1024), meta)
        import esptool
        from esptool import cmds
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        ram.require_port(args.port, args.serial, list(list_ports.comports()))
        with args.log.open("xb") as log, usb_console(serial, args.port) as wire:
            flash.record(log, f"FIRMWARE serial={args.serial}; boot-sha256={hashlib.sha256(boot).hexdigest()}; "
                         f"backup-sha256={hashlib.sha256(backup).hexdigest()}")
            esp = flash.connect(esptool, wire)
            if args.resume:
                for payload, (start, end) in ((xip, ranges[1]), (boot, ranges[0])):
                    flash.check(esp, start, payload + b"\xff" * (end - start - len(payload)))
                flash.record(log, "PASS: installed code pair verified; storage not written by loader")
                esptool.load_ram(esp, boot)
                capture_prompt(wire, log)
                flash.record(log, "PASS: installed firmware resumed through ROM RAM load")
                return 0
            if args.restore_original:
                restore(esp, cmds, backup, ranges, log)
                esptool.load_ram(esp, recovery)
                ram.capture(wire, 5, log, True)
                return 0
            if not args.ram_run:
                flash.require_flash_boot(esp)
            flash.check(esp, 0, backup)
            changed = False
            try:
                changed = True
                for payload, (start, end) in ((xip, ranges[1]), (boot, ranges[0])):
                    flash.program(esp, cmds, start, payload + b"\xff" * (end - start - len(payload)))
                expected = bytearray(backup)
                for payload, (start, end) in ((xip, ranges[1]), (boot, ranges[0])):
                    expected[start:end] = payload + b"\xff" * (end - start - len(payload))
                flash.check(esp, 0, expected)
                flash.record(log, "PASS: exact code installed; original storage unchanged before boot")
                if args.ram_run:
                    esptool.load_ram(esp, boot)
                else:
                    flash.boot_flash(esp)
                capture_prompt(wire, log)
                launch = "RAM load with flash companion" if args.ram_run else "flash boot"
                flash.record(log, f"PASS: normal firmware reached the shell after {launch}")
            except Exception as failure:
                flash.record(log, f"FAIL: {failure}; restoring first-install changes")
                if changed:
                    try:
                        esp = flash.connect(esptool, wire)
                        restore(esp, cmds, backup, ranges, log)
                        esptool.load_ram(esp, recovery)
                        ram.capture(wire, 5, log, True)
                    except Exception as error:
                        flash.record(log, f"RECOVERY FAILED: {error}; keep backup and use --restore-original")
                raise
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
