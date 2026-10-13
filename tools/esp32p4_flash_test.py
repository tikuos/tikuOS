#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32p4_flash_test.py - Guarded, backed-up P4 scratch-sector qualification.
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys
import time
import zlib

import esp32p4_ram as ram
from esp_usb_console import usb_console

SCRATCH = 0x00ff8000
SECTOR_SIZE = 4096
WRITE_COMMAND = b"!P4:WRITE:FF8000!\n"
VERIFY_COMMAND = b"!P4:VERIFY:FF8000!\n"
RESTORE_COMMAND = b"!P4:RESTORE:FF8000!\n"
ERASED_CRC = zlib.crc32(b"\xff" * SECTOR_SIZE)
MSPI_MARKER = b"P4MSPI READY flash=01000000 psram=02000000"


def require_mspi_image(meta):
    """Reject absent or duplicate coexistence capability before opening a port."""
    if sum(payload.count(MSPI_MARKER) for _, payload in meta["segments"]) != 1:
        raise ValueError("requires exactly one flash/PSRAM coexistence capability marker")


def pattern(seed):
    """Generate the exact native diagnostic's full-sector pattern."""
    return bytes((i * 29 ^ i >> 8 ^ seed) & 255 for i in range(SECTOR_SIZE))


def reference(data):
    """Require a blank scratch sector outside the original vendor partitions."""
    if len(data) != ram.FLASH_SIZE:
        raise ValueError("backup must be exactly 16 MiB")
    if data[SCRATCH:SCRATCH + SECTOR_SIZE] != b"\xff" * SECTOR_SIZE:
        raise ValueError("backup scratch sector is not erased; no write is permitted")
    table = data[0x10000:0x11000]
    entries = []
    for at in range(0, len(table), 32):
        entry = table[at:at + 32]
        if entry[:16] == b"\xeb\xeb" + b"\xff" * 14:
            if not entries or entry[16:] != hashlib.md5(table[:at]).digest():
                raise ValueError("vendor partition-table checksum is invalid")
            break
        if entry[:2] != b"\xaa\x50":
            raise ValueError("requires the vendor partition table at 0x10000")
        start, size = struct.unpack_from("<II", entry, 4)
        if (not size or start < 0x11000 or start + size > ram.FLASH_SIZE or
                start < SCRATCH + SECTOR_SIZE and start + size > SCRATCH or
                any(start < end and start + size > begin for begin, end in entries)):
            raise ValueError("partition bounds overlap scratch, metadata or another partition")
        entries.append((start, start + size))
    else:
        raise ValueError("vendor partition table lacks its checksum")
    changed = data[:SCRATCH] + pattern(0x5a) + data[SCRATCH + SECTOR_SIZE:]
    return {"md5": hashlib.md5(data).hexdigest(),
            "sha256": hashlib.sha256(data).hexdigest(),
            "pattern_md5": hashlib.md5(changed).hexdigest(),
            "prefix_md5": hashlib.md5(data[:SCRATCH]).hexdigest(),
            "suffix_md5": hashlib.md5(data[SCRATCH + SECTOR_SIZE:]).hexdigest()}


def validate_write_capture(data):
    """Require each native write assertion, pattern CRC, status and live IRQs."""
    if any(word in data for word in (b"FAIL:", b"TRAP", b"ESP-ROM:", b"REFUSED")):
        raise ValueError("scratch test failed, faulted, reset or refused its command")
    lines = [line for line in data.splitlines() if line.startswith(b"P4WRITE")]
    if len(lines) != 10 or data.count(ram.SCRATCH_WRITE_MARKER + b"\r\n") != 1:
        raise ValueError("incomplete or repeated scratch-write test")
    begin = re.fullmatch(rb"P4WRITE BEGIN offset=00ff8000 size=00001000 status=([0-9a-f]{8})", lines[0])
    if not begin or int(begin[1], 16) > 0xffff or int(begin[1], 16) & 0xc01f:
        raise ValueError("scratch target or initial status mismatch")
    if lines[1:5] != [b"P4WRITE PASS " + name for name in
                      (b"blank", b"bounds", b"unaligned", b"bit_transition")]:
        raise ValueError("scratch edge checks missing or reordered")
    for index, (mhz, seed) in enumerate(zip((40, 90, 180, 360), (0x11, 0x22, 0x33, 0x5a))):
        expected = f"P4WRITE CLOCK mhz={mhz:08x} crc={zlib.crc32(pattern(seed)):08x}".encode()
        if lines[5 + index] != expected:
            raise ValueError("scratch CPU clock or pattern mismatch")
    end = re.fullmatch(rb"P4WRITE END crc=([0-9a-f]{8}) ticks=([0-9a-f]{8}) "
                       rb"irqs=([0-9a-f]{8}) status=([0-9a-f]{8})", lines[9])
    if (not end or int(end[1], 16) != zlib.crc32(pattern(0x5a)) or end[4] != begin[1] or
            not 0 < int(end[3], 16) <= int(end[2], 16) < 128 * 30 or
            not re.search(rb"(?m)^P4WRITE END [^\r\n]*\r?\n", data)):
        raise ValueError("scratch checksum, unchanged status or IRQ progress missing")
    return int(begin[1], 16)


def validate_mspi_capture(data):
    """Require dirty-PSRAM and flash-cache checks at each supported CPU rate."""
    expected = [b"P4MSPI READY flash=01000000 psram=02000000"]
    expected += [f"P4MSPI PASS mhz={mhz:08x} checks=00000002".encode()
                 for mhz in (40, 90, 180, 360)]
    if [line for line in data.splitlines() if line.startswith(b"P4MSPI")] != expected:
        raise ValueError("flash/PSRAM coexistence checks incomplete or reordered")


def validate_recovery(data, restored, status=None):
    """Require one complete retained/restored record and its expected CRC."""
    name = b"RESTORED" if restored else b"RETAINED"
    crc = ERASED_CRC if restored else zlib.crc32(pattern(0x5a))
    lines = [line for line in data.splitlines() if line.startswith(b"P4WRITE")]
    match = re.fullmatch(rb"P4WRITE " + name + rb" crc=([0-9a-f]{8}) status=([0-9a-f]{8})",
                         lines[0]) if len(lines) == 1 else None
    if (any(word in data for word in (b"FAIL:", b"TRAP", b"ESP-ROM:", b"REFUSED")) or
            not match or int(match[1], 16) != crc or int(match[2], 16) > 0xffff or
            int(match[2], 16) & 0xc01f or status is not None and int(match[2], 16) != status or
            not re.search(rb"(?m)^P4WRITE " + name + rb" [^\r\n]*\r?\n", data)):
        raise ValueError("scratch recovery record is incomplete or incorrect")


def record(log, message):
    """Print and flush a recoverable-operation record to the required new log."""
    print(message, flush=True)
    log.write((message + "\n").encode())
    log.flush()


def command(wire, log, request, marker):
    """Run a framed diagnostic with bounded output and a 30-second deadline."""
    wire.timeout = 0.1
    wire.write(request)
    data = bytearray()
    end = time.monotonic() + 30
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("scratch test output exceeds 64 KiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            log.write(chunk)
            log.flush()
        if b"FAIL:" in data or re.search(rb"(?m)^" + marker + rb" [^\r\n]*\r?\n", data):
            return data
    raise ValueError("scratch test did not complete within 30 seconds")


def rom(esptool, wire):
    """Reconnect only the already-owned P4 and revalidate silicon/security."""
    esp = ram.connect_rom(esptool, wire)
    ram.validate_target(esp)
    esp.flash_spi_attach(0)
    esp.flash_set_parameters(ram.FLASH_SIZE)
    if esp.flash_id() != 0x1840ef:
        raise ValueError("scratch test requires EF:4018 flash")
    return esp


def unchanged_outside(esp, backup):
    """Refuse restoration if bytes outside the scratch reservation changed."""
    if (esp.flash_md5sum(0, SCRATCH).lower() != backup["prefix_md5"] or
            esp.flash_md5sum(SCRATCH + SECTOR_SIZE,
                             ram.FLASH_SIZE - SCRATCH - SECTOR_SIZE).lower() != backup["suffix_md5"]):
        raise ValueError("bytes outside scratch differ; no automatic erase is permitted")


def restore(esptool, wire, image, backup, log, seconds):
    """Erase scratch only after checking other contents, then verify the whole chip."""
    esp = rom(esptool, wire)
    unchanged_outside(esp, backup)
    record(log, "RESTORE: outside-scratch digests match; erasing [0xff8000,0xff9000)")
    esptool.load_ram(esp, image)
    ram.capture(wire, seconds, log, True)
    validate_recovery(command(wire, log, RESTORE_COMMAND, b"P4WRITE RESTORED"), True)
    esp = rom(esptool, wire)
    ram.check_flash_rom(esp, backup, log)
    record(log, "PASS: original full-chip contents restored")
    return esp


def main():
    """Run only the scratch-write image; require a backup and a new log."""
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
    parser.add_argument("--restore-scratch", action="store_true",
                        help="erase only scratch after an interrupted test; requires its erased backup")
    args = parser.parse_args()
    try:
        if not args.kernel_test or not 5 <= args.seconds <= 30:
            raise ValueError("requires --kernel-test and --seconds 5..30")
        with args.image.open("rb") as stream:
            image = stream.read(ram.KERNEL_LOW_END - ram.RAM_START + 257)
        meta = ram.validate_image(image, True)
        require_mspi_image(meta)
        with args.elf.open("rb") as stream:
            elf = stream.read(4 * 1024 * 1024 + 1)
        if len(elf) > 4 * 1024 * 1024 or ram.has_scratch_writer(meta) != 1:
            raise ValueError("requires exactly one scratch-write capability marker")
        ram.validate_elf(elf, meta)
        if args.check_only:
            print("PASS: P4 scratch-write image/ELF; writes restricted to [0xff8000,0xff9000)")
            return 0
        if args.backup is None or args.log is None:
            raise ValueError("scratch writes require --backup and --log")
        with args.backup.open("rb") as stream:
            backup = reference(stream.read(ram.FLASH_SIZE + 1))
        import esptool
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        ram.require_port(args.port, args.serial, list(list_ports.comports()))
        with args.log.open("xb") as log, usb_console(serial, args.port) as wire:
            record(log, f"SCRATCH TEST [0x00ff8000,0x00ff9000); serial={args.serial}; "
                        f"image-sha256={hashlib.sha256(image).hexdigest()}; backup-sha256={backup['sha256']}")
            dirty = False
            try:
                if args.restore_scratch:
                    esp = restore(esptool, wire, image, backup, log, args.seconds)
                else:
                    esp = rom(esptool, wire)
                    ram.check_flash_rom(esp, backup, log)
                    record(log, "START: backup matches whole chip; scratch is erased and outside vendor partitions")
                    esptool.load_ram(esp, image)
                    before = ram.capture(wire, args.seconds, log, True)
                    dirty = True
                    result = command(wire, log, WRITE_COMMAND, b"P4WRITE END")
                    status = validate_write_capture(result)
                    validate_mspi_capture(result)
                    after = ram.capture(wire, args.seconds, log, True)
                    ram.validate_continuity(before, after)
                    if after[0][2] != before[-1][2] + 1:
                        raise ValueError("kernel restarted during scratch test")
                    esp = rom(esptool, wire)
                    if esp.flash_md5sum(0, ram.FLASH_SIZE).lower() != backup["pattern_md5"]:
                        raise ValueError("whole-chip contents differ from backup plus expected scratch pattern")
                    record(log, "PASS: ROM digest matches backup plus scratch pattern across reset")
                    esptool.load_ram(esp, image)
                    ram.capture(wire, args.seconds, log, True)
                    validate_recovery(command(wire, log, VERIFY_COMMAND, b"P4WRITE RETAINED"), False, status)
                    esp = restore(esptool, wire, image, backup, log, args.seconds)
                    dirty = False
                esptool.load_ram(esp, image)
                ram.capture(wire, args.seconds, log, True)
            except Exception:
                if dirty:
                    record(log, "RECOVERY: test failed; attempting bounded scratch-only restoration")
                    try:
                        esp = restore(esptool, wire, image, backup, log, args.seconds)
                        esptool.load_ram(esp, image)
                        ram.capture(wire, args.seconds, log, True)
                    except Exception as recovery_error:
                        record(log, f"RESTORE FAILED: {recovery_error}; retain backup and use --restore-scratch")
                raise
        print("PASS: P4 scratch qualification; original full-chip contents restored")
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
