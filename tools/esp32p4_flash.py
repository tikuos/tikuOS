#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32p4_flash.py - Backup-checked native P4 boot installation and restoration.
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
from pathlib import Path
import sys
import time

import esp32p4_ram as ram
import esp32p4_xip as pairing
from esp32p4_layout import LAYOUT
from esp_usb_console import usb_console


def image_extent(data, xip=None):
    """Validate the storage-free kernel image and its exact sector write extent."""
    meta = ram.validate_image(data, True)
    if ram.has_firmware(meta):
        raise ValueError("normal firmware requires the storage-aware firmware installer")
    if ram.has_scratch_writer(meta):
        raise ValueError("scratch-write diagnostics must not be installed")
    if ram.has_storage_writer(meta):
        raise ValueError("storage-write diagnostics must not be installed")
    pairing.validate_pair(data, xip)
    start, limit = LAYOUT["BOOT"]
    size = (len(data) + 4095) & ~4095
    if not size or size > limit - start:
        raise ValueError("boot image exceeds its reserved capacity")
    return start, start + size


def connect(esptool, wire):
    """Require the exact supported ROM, boot offset, security and flash geometry."""
    esp = ram.connect_rom(esptool, wire)
    ram.validate_target(esp)
    if esp.BOOTLOADER_FLASH_OFFSET != LAYOUT["BOOT"][0]:
        raise ValueError("unexpected P4 ROM boot offset")
    esp.flash_spi_attach(0)
    esp.flash_set_parameters(ram.FLASH_SIZE)
    if esp.flash_id() != 0x1840ef:
        raise ValueError("requires the board's 16 MiB EF:4018 flash")
    return esp


def digest(data):
    """Compute the full-width digest supported by the ROM loader."""
    return hashlib.md5(data).hexdigest()


def require_flash_boot(esp):
    """Refuse installation when USB reset cannot leave the sampled download mode."""
    if not esp.read_reg(0x500e0038) & 8:
        raise ValueError("BOOT was sampled low; power-cycle with BOOT released before installation")


def boot_flash(esp):
    """Clear the volatile download request and reset with both USB lines released."""
    require_flash_boot(esp)
    address = 0x50110008
    value = esp.read_reg(address) & ~4
    esp.write_reg(address, value)
    if esp.read_reg(address) & 4:
        raise ValueError("could not clear forced ROM download")
    esp._port.setRTS(False)
    esp._port.setDTR(False)
    esp.hard_reset()


def wait_boot(wire, log):
    """Record ROM startup and require the native kernel banner within 8 seconds."""
    data = bytearray()
    end = time.monotonic() + 8
    marker = b"TikuOS ESP32-P4 RAM kernel test; CPU0 360 MHz; integer ABI\r\n"
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if chunk:
            log.write(chunk)
            log.flush()
        if len(data) > 16384 or any(x in data for x in (b"FAIL:", b"TRAP ", b"waiting for download")):
            raise ValueError("native flash startup failed")
        if marker in data:
            if data.count(b"ESP-ROM:") != 1 or b"SPI_FAST_FLASH_BOOT" not in data:
                raise ValueError("native banner lacks an unambiguous ROM flash boot")
            record(log, "PASS: native kernel startup banner after ROM flash boot")
            return
    raise ValueError("no native kernel banner after flash reset")


def capture_xip(wire, log):
    """Require paired flash execution at four rates without resets or faults."""
    data = bytearray()
    end = time.monotonic() + 8
    wire.write(b"X")
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if chunk:
            log.write(chunk)
            log.flush()
        if len(data) > 16384 or any(x in data for x in (b"FAIL", b"REFUSED", b"TRAP ", b"ESP-ROM:")):
            raise ValueError("XIP execution failed or companion refused")
        if b"P4XIP END\r\n" in data:
            rows = [line for line in data.splitlines() if line.startswith(b"P4XIP ")]
            if rows != [b"P4XIP BEGIN"] + [b"P4XIP PASS clock"] * 4 + [b"P4XIP END"]:
                raise ValueError("incomplete XIP clock execution checks")
            record(log, "PASS: matching XIP code and constants fetched at four CPU rates with live IRQs")
            return
    raise ValueError("XIP execution timed out")


def check(esp, start, data):
    """Require a ROM digest match for an exact, nonempty physical interval."""
    if not data or esp.flash_md5sum(start, len(data)).lower() != digest(data):
        raise ValueError(f"flash digest mismatch at 0x{start:x}, length 0x{len(data):x}")


def record(log, text):
    """Flush each installation or recovery record before the next operation."""
    print(text, flush=True)
    log.write((text + "\n").encode())
    log.flush()


def program(esp, commands, start, data):
    """Program and verify supplied sectors without erase-all or security overrides."""
    if start % 4096 or len(data) % 4096 or not data:
        raise ValueError("installer writes must contain complete aligned sectors")
    if not any(low <= start < start + len(data) <= high
               for low, high in (LAYOUT["BOOT"], LAYOUT["XIP"])):
        raise ValueError("installer write is outside the code reservations")
    entries = [(start, data)]
    commands.write_flash(esp, entries, flash_size="keep", flash_mode="keep",
                         flash_freq="keep", erase_all=False, force=False,
                         encrypt=False, no_compress=True)
    commands.verify_flash(esp, entries)
    check(esp, start, data)


def restore(esp, commands, backup, extent, log, xip_extent=None):
    """Restore exact changed code sectors after checking every unchanged interval."""
    extents = [extent] + ([xip_extent] if xip_extent else [])
    previous = 0
    for start, end in extents:
        if start < previous or end <= start or end > len(backup):
            raise ValueError("invalid recovery extent")
        if start > previous:
            check(esp, previous, backup[previous:start])
        previous = end
    if previous < len(backup):
        check(esp, previous, backup[previous:])
    record(log, f"RESTORE: verified unchanged intervals; restoring {extents}")
    for start, end in reversed(extents):
        program(esp, commands, start, backup[start:end])
    check(esp, 0, backup)
    record(log, "PASS: original full 16 MiB flash contents restored")


def main():
    """Require an explicit first install, complete matching backup and new log."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--xip", type=Path)
    parser.add_argument("--psram-check", action="store_true",
                        help="test unallocated PSRAM after native boot; requires P4_PSRAM_TEST=1")
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--first-install", action="store_true")
    parser.add_argument("--restore-after", action="store_true")
    parser.add_argument("--restore-original", action="store_true")
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--port")
    parser.add_argument("--serial")
    args = parser.parse_args()
    try:
        with args.image.open("rb") as stream:
            data = stream.read(ram.KERNEL_LOW_END - ram.RAM_START + 257)
        xip = None
        if args.xip:
            with args.xip.open("rb") as stream:
                xip = stream.read(pairing.XIP_CAPACITY + 1)
        extent = image_extent(data, xip)
        if args.psram_check:
            if xip:
                raise ValueError("--psram-check requires a SRAM-only qualification image")
            ram.require_psram_image(ram.validate_image(data, True))
        xip_extent = (pairing.XIP_OFFSET, pairing.XIP_OFFSET + ((len(xip) + 4095) & ~4095)) if xip else None
        with args.elf.open("rb") as stream:
            elf = stream.read(4 * 1024 * 1024 + 1)
        if len(elf) > 4 * 1024 * 1024:
            raise ValueError("ELF exceeds size bound")
        if xip:
            pairing.validate_elf(elf, data, xip)
        else:
            ram.validate_elf(elf, ram.validate_image(data, True))
        start, end = extent
        print(f"P4 native boot write sectors [0x{start:08x},0x{end:08x}); no storage erase")
        if xip_extent:
            print(f"P4 companion write sectors [0x{xip_extent[0]:08x},0x{xip_extent[1]:08x})")
        if args.check_only:
            return 0
        if args.first_install == args.restore_original or not args.backup or not args.log:
            raise ValueError("requires explicit --first-install or --restore-original, --backup and --log")
        if args.restore_after and not args.first_install:
            raise ValueError("--restore-after requires --first-install")
        with args.backup.open("rb") as stream:
            backup = stream.read(ram.FLASH_SIZE + 1)
        if len(backup) != ram.FLASH_SIZE:
            raise ValueError("backup must be exactly 16 MiB")
        import esptool
        from esptool import cmds
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        ram.require_port(args.port, args.serial, list(list_ports.comports()))
        with args.log.open("xb") as log, usb_console(serial, args.port) as wire:
            record(log, f"BOOT [0x{start:x},0x{end:x}); serial={args.serial}; "
                   f"image-sha256={hashlib.sha256(data).hexdigest()}; backup-sha256={hashlib.sha256(backup).hexdigest()}")
            esp = connect(esptool, wire)
            if args.restore_original:
                restore(esp, cmds, backup, extent, log, xip_extent)
                esptool.load_ram(esp, data)
                ram.capture(wire, 5, log, True)
                return 0
            require_flash_boot(esp)
            check(esp, 0, backup)
            padded = data + b"\xff" * (end - start - len(data))
            expected = bytearray(backup[:start] + padded + backup[end:])
            if xip:
                xip_padded = xip + b"\xff" * (xip_extent[1] - xip_extent[0] - len(xip))
                expected[xip_extent[0]:xip_extent[1]] = xip_padded
            changed = False
            try:
                record(log, "START: full-chip backup matches; programming native boot sectors")
                changed = True
                if xip:
                    record(log, f"START: companion sectors {xip_extent}; sha256={hashlib.sha256(xip).hexdigest()}")
                    program(esp, cmds, xip_extent[0], xip_padded)
                program(esp, cmds, start, padded)
                check(esp, 0, expected)
                record(log, "PASS: complete flash matches backup plus exact native boot extent")
                boot_flash(esp)
                wait_boot(wire, log)
                first = ram.capture(wire, 5, log, True)
                second = ram.capture(wire, 5, log, True)
                ram.validate_continuity(first, second)
                if second[0][2] != first[-1][2] + 1:
                    raise ValueError("kernel restarted after flash boot")
                record(log, "PASS: native flash boot and two complete CPU0 kernel runs; no RAM download")
                if xip:
                    capture_xip(wire, log)
                if args.psram_check:
                    ram.capture_psram(wire, log)
                if xip or args.psram_check:
                    after = ram.capture(wire, 5, log, True)
                    ram.validate_continuity(second, after)
                    if after[0][2] != second[-1][2] + 1:
                        raise ValueError("kernel restarted during external-memory qualification")
                if args.restore_after:
                    esp = connect(esptool, wire)
                    check(esp, 0, expected)
                    restore(esp, cmds, backup, extent, log, xip_extent)
                    changed = False
                    esptool.load_ram(esp, data)
                    ram.capture(wire, 5, log, True)
            except Exception as failure:
                record(log, f"FAIL: qualification interrupted: {failure}")
                if changed:
                    try:
                        esp = connect(esptool, wire)
                        restore(esp, cmds, backup, extent, log, xip_extent)
                        esptool.load_ram(esp, data)
                        ram.capture(wire, 5, log, True)
                    except Exception as error:
                        record(log, f"RESTORE FAILED: {error}; retain backup and use --restore-original")
                raise
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
