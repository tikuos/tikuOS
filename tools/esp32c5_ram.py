#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32c5_ram.py - validate and RAM-load C5 diagnostics or kernel images.
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

CHIP_ID = 23
RAM_START = 0x40800000
RAM_LIMIT = 0x4080F000
MIN_REVISION = 100
MAX_REVISION = 199
MIN_ROM_ECO = 2


def validate_image(data, profile="diagnostic"):
    """Validate the C5 image header, loaded ranges, checksum and SHA-256."""
    if profile not in ("diagnostic", "kernel"):
        raise ValueError("unknown C5 image profile")
    limit = RAM_LIMIT if profile == "diagnostic" else 0x4084A5A0
    if len(data) < 24 or len(data) > limit - RAM_START + 256:
        raise ValueError("invalid RAM image size")
    magic, count, _, _, entry = struct.unpack_from("<BBBBI", data)
    if magic != 0xE9 or not 1 <= count <= 16:
        raise ValueError("invalid image header or segment count")
    chip = struct.unpack_from("<H", data, 12)[0]
    minimum, maximum = struct.unpack_from("<HH", data, 15)
    if chip != CHIP_ID:
        raise ValueError("image chip must be ESP32-C5 (23)")
    if minimum < MIN_REVISION or maximum < minimum or maximum > MAX_REVISION:
        raise ValueError("image revision range must lie within v1.0 to v1.99")
    if data[23] != 1:
        raise ValueError("RAM image requires an appended SHA-256 digest")
    offset, checksum, ranges = 24, 0xEF, []
    for _ in range(count):
        if offset + 8 > len(data):
            raise ValueError("truncated segment header")
        address, size = struct.unpack_from("<II", data, offset)
        offset += 8
        end = address + size
        if (size == 0 or address % 4 or size % 4 or
                address < RAM_START or end > limit):
            raise ValueError("segment outside profile RAM or unaligned")
        if any(address < high and end > low for low, high in ranges):
            raise ValueError("overlapping RAM segments")
        if offset + size > len(data):
            raise ValueError("truncated segment data")
        for byte in data[offset:offset + size]:
            checksum ^= byte
        ranges.append((address, end))
        offset += size
    if entry % 2 or not any(low <= entry < high for low, high in ranges):
        raise ValueError("entry must be aligned and inside a loaded segment")
    checksum_offset = offset + 15 - offset % 16
    digest_offset = checksum_offset + 1
    if len(data) != digest_offset + 32:
        raise ValueError("truncated image or unexpected trailing bytes")
    if any(data[offset:checksum_offset]) or data[checksum_offset] != checksum:
        raise ValueError("invalid padding or segment checksum")
    if hashlib.sha256(data[:digest_offset]).digest() != data[digest_offset:]:
        raise ValueError("image SHA-256 mismatch")
    return {"entry": entry, "min_revision": minimum,
            "max_revision": maximum, "segments": ranges, "profile": profile}


def validate_identity(chip, eco, revision, image):
    """Refuse another chip, an incompatible ROM or an out-of-range revision."""
    if chip != CHIP_ID:
        raise ValueError("connected chip is not ESP32-C5")
    if eco < MIN_ROM_ECO:
        raise ValueError("ESP32-C5 ROM ECO 2 or later is required")
    if not image["min_revision"] <= revision <= image["max_revision"]:
        raise ValueError("connected silicon revision is outside the image range")


def require_idle_usb_port(port, ports):
    """Require a named, idle native Espressif USB port before opening it."""
    resolved = os.path.realpath(port)
    if not any(os.path.realpath(item.device) == resolved and
               (item.vid, item.pid) == (0x303A, 0x1001) for item in ports):
        raise ValueError("port is not a native Espressif USB Serial/JTAG device")
    if not shutil.which("fuser"):
        raise ValueError("fuser is required to check existing port owners")
    # Linux fuser returns 1 and prints nothing for an idle file; macOS fuser
    # returns 0 and echoes "<path>:" on stdout.  Both print owner PIDs after
    # that, so once the echoed path is removed any remaining text is an owner
    # or an error message.
    owners = subprocess.run(["fuser", resolved], capture_output=True, text=True)
    report = (owners.stdout + owners.stderr).replace(resolved + ":", "")
    if owners.returncode > 1 or report.strip():
        raise ValueError("port is busy or its ownership could not be checked")


def validate_capture(captured):
    """Require RAM, nested masking, USB and advancing interrupt-driven ticks."""
    heartbeats = [int(value, 16) for value in
                  re.findall(rb"heartbeat=0x([0-9a-f]{8})\r?\n", captured)]
    if (b"PASS: data, bss, SRAM read/write" not in captured or
            b"PASS: nested IRQ mask and delayed ticks" not in captured or
            b"PASS: IRQ register frame" not in captured or
            b"PONG\r\n" not in captured or len(heartbeats) < 2 or
            heartbeats[-1] <= heartbeats[0] or
            any(b < a for a, b in zip(heartbeats, heartbeats[1:])) or
            b"FAIL:" in captured or b"TRAP " in captured or
            b"ESP-ROM:" in captured):
        raise ValueError("RAM diagnostic markers missing, stalled or failed")
    counters = timer_counters(captured)
    if (len(counters) < 2 or counters[-1][0] <= counters[0][0] or
            counters[-1][1] <= counters[0][1] or
            any(row[2] or row[3] for row in counters) or
            any(b[0] < a[0] or b[1] < a[1]
                for a, b in zip(counters, counters[1:]))):
        raise ValueError("C5 timer counters missing, stalled, reset or failed")


def timer_counters(captured):
    """Extract tick, interrupt, snapshot-error and unclaimed-interrupt counters."""
    return [tuple(int(value, 16) for value in row) for row in re.findall(
        rb"ticks=0x([0-9a-f]{8}) interrupts=0x([0-9a-f]{8}) "
        rb"timer_errors=0x([0-9a-f]{8}) unclaimed=0x([0-9a-f]{8})\r?\n",
        captured)]


def usb_console(serial, port):
    """Construct a closed console with both control lines asserted on open.

    On native USB, deasserting DTR while RTS is asserted resets the C5.
    The ROM loader changes these lines explicitly when a reset is requested.
    """
    wire = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                         write_timeout=1, exclusive=True)
    wire.dtr = True
    wire.rts = True
    wire.port = port
    return wire


def capture_console(wire, seconds, log_file):
    """Request identity, ping and the IRQ test; capture bounded diagnostic output."""
    wire.timeout = 0.1
    wire.write(b"ipcr")
    captured = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        chunk = wire.read(1024)
        if chunk:
            captured.extend(chunk)
            if len(captured) > 1024 * 1024:
                raise ValueError("diagnostic output exceeded 1 MiB")
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log_file is not None:
                log_file.write(chunk)
                log_file.flush()
    validate_capture(captured)
    return captured


def kernel_ticks(captured):
    """Read ticks from the shell's info response, ignoring terminal colors."""
    clean = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", captured)
    return [int(tick) for tick in re.findall(rb"Clock:\s+128 ticks/sec \(now (\d+)\)", clean)]


def validate_kernel_capture(captured):
    """Require C5 identity and progressing kernel ticks without a reported fault."""
    ticks = kernel_ticks(captured)
    if (b"ESP32-C5" not in captured or len(ticks) < 2 or ticks[-1] <= ticks[0] or
            any(b < a for a, b in zip(ticks, ticks[1:])) or
            b"TRAP " in captured or b"FATAL" in captured or b"FAIL:" in captured or
            b"C5 HALT:" in captured):
        raise ValueError("C5 kernel identity or progressing clock missing, or fault reported")


def capture_kernel(wire, seconds, log_file):
    """Request shell identity and tick progress, with a bounded output capture."""
    captured = bytearray()
    end = time.monotonic() + seconds
    next_query = time.monotonic() + 0.5
    wire.timeout = 0.1
    while time.monotonic() < end:
        if time.monotonic() >= next_query:
            wire.write(b"info\r")
            next_query = time.monotonic() + 0.75
        chunk = wire.read(4096)
        captured.extend(chunk)
        if len(captured) > 1024 * 1024:
            raise ValueError("kernel output exceeded 1 MiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log_file is not None:
                log_file.write(chunk)
                log_file.flush()
    validate_kernel_capture(captured)
    return captured


def load_and_monitor(port, data, meta, seconds, log_file, reopen=0):
    """Load through the ROM and check diagnostic or kernel console progress."""
    import esptool
    import serial
    from serial.tools import list_ports

    if esptool.__version__ != "5.1.0":
        raise ValueError("this loader requires esptool==5.1.0")
    require_idle_usb_port(port, list_ports.comports())
    wire = usb_console(serial, port)
    with wire:
        esp = esptool.detect_chip(wire, connect_attempts=3)
        if esp.CHIP_NAME != "ESP32-C5" or esp.IS_STUB:
            raise ValueError("requires an ESP32-C5 ROM loader, not a stub")
        rom_chip = esp.read_reg(0x40000010)
        eco = esp.read_reg(0x40000014)
        revision = esp.get_major_chip_version() * 100 + esp.get_minor_chip_version()
        validate_identity(rom_chip, eco, revision, meta)
        print(f"C5 revision={revision} ROM_ECO={eco}; loading RAM only")
        esptool.load_ram(esp, data)
        capture = capture_kernel if meta["profile"] == "kernel" else capture_console
        captured = capture(wire, seconds, log_file)
    counters = (lambda data: [(n,) for n in kernel_ticks(data)]) if meta["profile"] == "kernel" else timer_counters
    previous = counters(captured)[-1]
    for _ in range(reopen):
        require_idle_usb_port(port, list_ports.comports())
        with usb_console(serial, port) as wire:
            captured = capture(wire, seconds, log_file)
        current = counters(captured)
        if any(a < b for a, b in zip(current[0], previous)):
            raise ValueError("C5 reset while reopening the console")
        previous = current[-1]
    print("PASS: C5 console and timer progress; loader issued no flash writes")


def main():
    """Check offline by default; --port explicitly authorizes a RAM-load session."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--port")
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--profile", choices=("diagnostic", "kernel"), default="diagnostic")
    parser.add_argument("--reopen", type=int, default=0,
                        help="close and reopen USB 0 to 5 times after RAM boot")
    args = parser.parse_args()
    try:
        with args.image.open("rb") as image_file:
            data = image_file.read(384 * 1024 + 1)
        meta = validate_image(data, args.profile)
        if args.check:
            print(f"C5 RAM image OK: {len(data)} bytes, entry={meta['entry']:#x}")
            return 0
        if args.profile == "kernel":
            import esp32c5_xip
            if esp32c5_xip.descriptor_offset(data) is not None:
                raise ValueError("XIP builds require the paired flash installer, not RAM loading")
        if not 2 <= args.seconds <= 60:
            raise ValueError("monitor duration must be between 2 and 60 seconds")
        if not 0 <= args.reopen <= 5:
            raise ValueError("reopen count must be between 0 and 5")
        if args.log is None:
            load_and_monitor(args.port, data, meta, args.seconds, None, args.reopen)
        else:
            with args.log.open("xb") as log_file:
                load_and_monitor(args.port, data, meta, args.seconds, log_file,
                                 args.reopen)
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"C5 loader: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
