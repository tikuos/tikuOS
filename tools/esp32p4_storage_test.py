#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_storage_test.py - Backed-up P4 NVM region and mirror qualification.
#
# RAM-loads the storage-test image to write 8 KiB of the NVM region and
# both mirror slots, checks the whole chip and the retained data across a
# reset, then erases the ten test sectors again.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
from pathlib import Path
import struct
import sys
import zlib

import esp32p4_ram as ram
import esp32p4_flash_test as scratch
from esp32p4_layout import LAYOUT
from esp_usb_console import usb_console

EXTENTS = ((LAYOUT["REGION"][0], LAYOUT["REGION"][0] + 8192),
           (LAYOUT["MIRROR_A"][0], LAYOUT["MIRROR_B"][1]))
WRITE = b"!P4:STORE:400000!\n"
VERIFY = b"!P4:RETAIN:400000!\n"
CLEAR = b"!P4:CLEAR:400000!\n"


def expected_contents(data):
    """Construct the exact region, unpublished A slot and committed B slot."""
    changed = bytearray(data)
    start = EXTENTS[0][0]
    changed[start:start + 8192] = bytes((i * 29 ^ i >> 8 ^ 0x66) & 255 for i in range(8192))
    changed[start + 4090:start + 4103] = b"\xff" * 13
    start = LAYOUT["MIRROR_A"][0]
    changed[start + 16:start + 24] = b"TORNP4!!"
    payload = bytes((i * 29 ^ i >> 8 ^ 0x52) & 255 for i in range(5000))
    start = LAYOUT["MIRROR_B"][0]
    changed[start:start + 16] = struct.pack("<4I", 0x324d564e, zlib.crc32(payload), 5000, 2)
    changed[start + 16:start + 16 + len(payload)] = payload
    return changed


def reference(data):
    """Require erased storage, a valid vendor partition table and no overlap."""
    backup = scratch.reference(data)
    start, end = LAYOUT["REGION"][0], LAYOUT["MIRROR_B"][1]
    if data[start:end] != b"\xff" * (end - start):
        raise ValueError("entire writable storage reservation must be erased in the backup")
    table = data[0x10000:0x11000]
    for at in range(0, len(table), 32):
        if table[at:at + 2] != b"\xaa\x50":
            break
        base, size = struct.unpack_from("<II", table, at + 4)
        if base < end and base + size > start:
            raise ValueError("vendor partition overlaps the writable storage reservation")
    gaps = ((0, EXTENTS[0][0]), (EXTENTS[0][1], EXTENTS[1][0]),
            (EXTENTS[1][1], ram.FLASH_SIZE))
    backup["gaps"] = [(a, b, hashlib.md5(data[a:b]).hexdigest()) for a, b in gaps]
    backup["storage_md5"] = hashlib.md5(expected_contents(data)).hexdigest()
    return backup


def validate_capture(data, operation):
    """Require exact ordered assertions and reject faults, resets and repeats."""
    records = {
        "write": [b"P4STORE PASS blank", b"P4STORE PASS region", b"P4STORE PASS mirror",
                  b"P4STORE END commits=00000002"],
        "verify": [b"P4STORE RETAINED bytes=00001388"],
        "restore": [b"P4STORE RESTORED sectors=0000000a"],
    }
    if operation not in records:
        raise ValueError("unknown storage operation")
    lines = [line for line in data.splitlines() if line.startswith(b"P4STORE")]
    if (any(word in data for word in (b"FAIL:", b"TRAP", b"ESP-ROM:", b"REFUSED")) or
            lines != records[operation] or not data.endswith(b"\n") or
            (operation == "write" and data.count(ram.STORAGE_TEST_MARKER + b"\r\n") != 1)):
        raise ValueError("storage assertions incomplete, duplicated or failed")


def restore(esptool, wire, image, backup, log, seconds):
    """Check untouched ranges before erasing precisely the ten test sectors."""
    esp = scratch.rom(esptool, wire)
    for start, end, digest in backup["gaps"]:
        if esp.flash_md5sum(start, end - start).lower() != digest:
            raise ValueError("bytes outside storage test extents changed; restoration refused")
    scratch.record(log, "RESTORE: outside-test digests match; erase [400000,402000), [ff0000,ff8000)")
    esptool.load_ram(esp, image)
    ram.capture(wire, seconds, log, True)
    validate_capture(scratch.command(wire, log, CLEAR, b"P4STORE RESTORED"), "restore")
    esp = scratch.rom(esptool, wire)
    ram.check_flash_rom(esp, backup, log)
    scratch.record(log, "PASS: original full-chip contents restored")
    return esp


def main():
    """Validate offline first; require an exact device, full backup and new log."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--kernel-test", action="store_true")
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--port")
    parser.add_argument("--serial")
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--restore-storage", action="store_true")
    args = parser.parse_args()
    try:
        if not args.kernel_test or not 5 <= args.seconds <= 30:
            raise ValueError("requires --kernel-test and --seconds 5..30")
        with args.image.open("rb") as stream:
            image = stream.read(ram.KERNEL_LOW_END - ram.RAM_START + 257)
        meta = ram.validate_image(image, True)
        if ram.has_storage_writer(meta) != 1 or ram.has_scratch_writer(meta):
            raise ValueError("requires exactly one storage-test marker and no scratch writer")
        with args.elf.open("rb") as stream:
            elf = stream.read(4 * 1024 * 1024 + 1)
        if len(elf) > 4 * 1024 * 1024:
            raise ValueError("ELF exceeds size bound")
        ram.validate_elf(elf, meta)
        if args.check_only:
            print("PASS: P4 storage image/ELF; writable reservation [0x400000,0xff8000)")
            return 0
        if args.backup is None or args.log is None:
            raise ValueError("storage qualification requires --backup and --log")
        with args.backup.open("rb") as stream:
            backup = reference(stream.read(ram.FLASH_SIZE + 1))
        import esptool
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        ram.require_port(args.port, args.serial, list(list_ports.comports()))
        with args.log.open("xb") as log, usb_console(serial, args.port) as wire:
            scratch.record(log, f"STORAGE TEST serial={args.serial}; image-sha256="
                           f"{hashlib.sha256(image).hexdigest()}; backup-sha256={backup['sha256']}")
            dirty = False
            try:
                if args.restore_storage:
                    esp = restore(esptool, wire, image, backup, log, args.seconds)
                else:
                    esp = scratch.rom(esptool, wire)
                    ram.check_flash_rom(esp, backup, log)
                    esptool.load_ram(esp, image)
                    before = ram.capture(wire, args.seconds, log, True)
                    dirty = True
                    validate_capture(scratch.command(wire, log, WRITE, b"P4STORE END"), "write")
                    after = ram.capture(wire, args.seconds, log, True)
                    ram.validate_continuity(before, after)
                    if after[0][2] != before[-1][2] + 1:
                        raise ValueError("kernel restarted during storage qualification")
                    esp = scratch.rom(esptool, wire)
                    if esp.flash_md5sum(0, ram.FLASH_SIZE).lower() != backup["storage_md5"]:
                        raise ValueError("whole-chip digest differs from exact expected storage contents")
                    scratch.record(log, "PASS: ROM digest matches exact region and mirror data across reset")
                    esptool.load_ram(esp, image)
                    ram.capture(wire, args.seconds, log, True)
                    validate_capture(scratch.command(wire, log, VERIFY, b"P4STORE RETAINED"), "verify")
                    esp = restore(esptool, wire, image, backup, log, args.seconds)
                    dirty = False
                esptool.load_ram(esp, image)
                ram.capture(wire, args.seconds, log, True)
            except Exception:
                if dirty:
                    scratch.record(log, "RECOVERY: attempting bounded storage-test restoration")
                    try:
                        esp = restore(esptool, wire, image, backup, log, args.seconds)
                        esptool.load_ram(esp, image)
                        ram.capture(wire, args.seconds, log, True)
                    except Exception as error:
                        scratch.record(log, f"RESTORE FAILED: {error}; retain backup; use --restore-storage")
                raise
        print("PASS: P4 storage qualification; original full-chip contents restored")
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
