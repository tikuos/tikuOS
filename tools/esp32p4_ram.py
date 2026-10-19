#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_ram.py - Guarded storage-free native P4 diagnostic loading.
#
# Validates a diagnostic or kernel-test image and its ELF before opening
# USB, RAM-loads it through the ROM and checks the console output, with
# optional fault, watchdog, flash and PSRAM checks; it programs no flash.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import sys
import time
import zlib

from esp_usb_console import require_idle_usb_port, usb_console

CHIP_ID = 18
REVISION = 103
ROM_ECO = 2
RAM_START = 0x4FF00000
RAM_LIMIT = 0x4FF0F000
FAULT_START = 0x5010FA40
FAULT_SIZE = 64
KERNEL_LOW_END = 0x4FF2BBD0
KERNEL_HIGH_START = 0x4FF40000
KERNEL_STACK = 0x4FFA0000
KERNEL_GUARD = KERNEL_STACK - 4096 - 64
FLASH_SIZE = 16 * 1024 * 1024
FLASH_BLOCK = 256 * 1024
SCRATCH_WRITE_MARKER = b"TIKU-P4-SCRATCH-WRITE-v1:00ff8000:00001000"
STORAGE_TEST_MARKER = b"TIKU-P4-STORAGE-TEST-v1:00400000:00bf8000"
PSRAM_TEST_MARKER = b"TIKU-P4-PSRAM-TEST-v1:32MiB:80MHz"
FIRMWARE_MARKER = b"TIKU-P4-FIRMWARE-v1:00400000:00bf8000"


def has_firmware(image):
    """Identify normal firmware, which may provision erased storage at boot."""
    return sum(payload.count(FIRMWARE_MARKER) for _, payload in image["segments"])


def has_scratch_writer(image):
    """Identify the explicit scratch-write test payload, not ordinary firmware."""
    return sum(payload.count(SCRATCH_WRITE_MARKER) for _, payload in image["segments"])


def has_storage_writer(image):
    """Identify the explicit storage qualification payload."""
    return sum(payload.count(STORAGE_TEST_MARKER) for _, payload in image["segments"])


def validate_image(data, kernel_test=False):
    """Check the exact P4 v1.3 SRAM profile, checksum and appended digest."""
    limit = KERNEL_LOW_END if kernel_test else RAM_LIMIT
    if not 24 <= len(data) <= limit - RAM_START + 256:
        raise ValueError("invalid P4 diagnostic image size")
    magic, count, mode, geometry, entry = struct.unpack_from("<BBBBI", data)
    if magic != 0xE9 or not 1 <= count <= 16:
        raise ValueError("invalid image header or segment count")
    if mode != 2 or geometry != 0x40:
        raise ValueError("diagnostic requires DIO, 40 MHz, 16 MiB header")
    if struct.unpack_from("<H", data, 12)[0] != CHIP_ID:
        raise ValueError("image must target ESP32-P4")
    if struct.unpack_from("<HH", data, 15) != (REVISION, REVISION):
        raise ValueError("image must target exactly P4 silicon v1.3")
    if data[23] != 1:
        raise ValueError("image requires an appended SHA-256")
    offset, checksum, segments = 24, 0xEF, []
    for _ in range(count):
        if offset + 8 > len(data):
            raise ValueError("truncated segment header")
        address, size = struct.unpack_from("<II", data, offset)
        offset += 8
        if (not size or address % 4 or size % 4 or address < RAM_START or
                address + size > limit or offset + size > len(data)):
            raise ValueError("segment outside diagnostic SRAM or malformed")
        if any(address < base + len(payload) and address + size > base
               for base, payload in segments):
            raise ValueError("overlapping segments")
        payload = data[offset:offset + size]
        segments.append((address, payload))
        for byte in payload:
            checksum ^= byte
        offset += size
    if entry % 2 or not any(a <= entry < a + len(p) for a, p in segments):
        raise ValueError("entry is not aligned inside a loaded segment")
    checksum_at = offset + 15 - offset % 16
    digest_at = checksum_at + 1
    if len(data) != digest_at + 32:
        raise ValueError("truncated image or unexpected trailing data")
    if any(data[offset:checksum_at]) or data[checksum_at] != checksum:
        raise ValueError("invalid checksum or nonzero padding")
    if hashlib.sha256(data[:digest_at]).digest() != data[digest_at:]:
        raise ValueError("SHA-256 mismatch")
    return {"entry": entry, "segments": segments, "kernel_test": kernel_test}


def validate_elf(data, image):
    """Require RV32 soft-float ELF and matching SRAM sections and entry."""
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("requires little-endian ELF32")
    kind, machine, version, entry, _, shoff, flags = struct.unpack_from(
        "<HHIIIII", data, 16)
    ehsize, _, _, shsize, shcount, _ = struct.unpack_from("<HHHHHH", data, 40)
    if (kind != 2 or machine != 243 or version != 1 or flags & ~1 or
            entry != image["entry"] or ehsize != 52 or shsize != 40 or
            not shcount or shoff + shsize * shcount > len(data)):
        raise ValueError("requires matching RV32 ilp32 executable")
    entry_executable, attributes, allocated = False, False, []
    fault_section = False
    kernel = image.get("kernel_test", False)
    firmware = image.get("firmware", False)
    stack_size = 16384 if firmware else 4096
    guard = KERNEL_STACK - stack_size - 64
    if firmware and (not kernel or has_firmware(image) != 1):
        raise ValueError("firmware requires its unique storage-writing profile marker")
    for index in range(shcount):
        _, kind, flags, address, offset, size, _, _, _, _ = struct.unpack_from(
            "<10I", data, shoff + index * shsize)
        if kind != 8 and offset + size > len(data):
            raise ValueError("truncated ELF section")
        if kind == 0x70000003:
            isa = re.findall(rb"rv[0-9]+[^\x00]*", data[offset:offset + size])
            required = {b"rv32i", b"m", b"a", b"c", b"zicsr", b"zifencei"}
            allowed = required | {b"zmmul", b"zaamo", b"zalrsc", b"zca"}
            parts = isa[0].split(b"_") if len(isa) == 1 else []
            names = {re.sub(rb"[0-9]+p[0-9]+$", b"", p) for p in parts}
            attributes = required <= names <= allowed and all(
                re.search(rb"[0-9]+p[0-9]+$", p) for p in parts)
        if not flags & 2 or not size:
            continue
        stack_section = kernel and kind == 8 and flags == 3 and (
            (address, size) in ((guard, 64), (KERNEL_STACK - stack_size, stack_size)))
        retained = kind == 8 and flags == 3 and (address, size) == (FAULT_START, FAULT_SIZE)
        fault_section |= retained
        if not (stack_section or retained) and (address < RAM_START or address + size >
                                  (KERNEL_LOW_END if kernel else RAM_LIMIT)):
            raise ValueError("allocated ELF section overlaps reserved memory")
        if any(address < end and address + size > start
               for start, end in allocated):
            raise ValueError("overlapping allocated ELF sections")
        allocated.append((address, address + size))
        if kind == 8:
            if any(address < base + len(payload) and address + size > base
                   for base, payload in image["segments"]):
                raise ValueError("image overwrites ELF BSS")
            continue
        if kind != 1 or not any(
                base <= address and address + size <= base + len(payload) and
                payload[address - base:address - base + size] ==
                data[offset:offset + size] for base, payload in image["segments"]):
            raise ValueError("ELF section is absent from image or mismatched")
        entry_executable |= bool(flags & 4 and address <= entry < address + size)
    if not entry_executable or not attributes or not fault_section:
        raise ValueError("missing executable entry, fault reservation or supported ISA")
    if kernel:
        return validate_kernel_layout(data, allocated, image["segments"], firmware)


def validate_kernel_layout(data, allocated, segments, firmware=False):
    """Cross-check linker span, BSS, stack and guard symbols before ROM load."""
    shoff = struct.unpack_from("<I", data, 32)[0]
    shcount = struct.unpack_from("<H", data, 48)[0]
    sections = [struct.unpack_from("<10I", data, shoff + i * 40)
                for i in range(shcount)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        _, _, _, _, offset, size, link, _, _, entry_size = section
        if entry_size != 16 or size % 16 or link >= shcount:
            raise ValueError("invalid kernel symbol table")
        strings = sections[link]
        if strings[1] != 3:
            raise ValueError("missing kernel symbol names")
        names = data[strings[4]:strings[4] + strings[5]]
        for at in range(offset, offset + size, 16):
            name, value, _, info, _, defined = struct.unpack_from("<IIIBBH", data, at)
            if name >= len(names) or b"\0" not in names[name:]:
                raise ValueError("invalid kernel symbol name")
            name = bytes(names[name:names.index(0, name)])
            if defined and info >> 4 == 1:
                if name in symbols:
                    raise ValueError("duplicate kernel symbol")
                symbols[name] = value
    stack_size = 16384 if firmware else 4096
    guard_address = KERNEL_STACK - stack_size - 64
    required = {
        b"__tier_sram_start": KERNEL_HIGH_START,
        b"__tier_sram_end": guard_address,
        b"__tier_sram_extra_end": KERNEL_LOW_END,
        b"__stack_guard": guard_address,
        b"__stack_bottom": KERNEL_STACK - stack_size,
        b"__stack": KERNEL_STACK,
    }
    if any(symbols.get(name) != value for name, value in required.items()):
        raise ValueError("kernel SRAM/stack boundaries disagree with loader")
    start, end = symbols.get(b"__bss_start", 0), symbols.get(b"__bss_end", 0)
    low_end = max((end for start, end in allocated if start < KERNEL_LOW_END),
                  default=0)
    statics_end = symbols.get(b"__retained_end", 0) if firmware else end
    if firmware:
        retained_start = symbols.get(b"__retained_start", 0)
        durable_start = symbols.get(b"__uninit_start", 0)
        durable_end = symbols.get(b"__uninit_end", 0)
        if (retained_start != end or not end <= statics_end <= KERNEL_LOW_END or
                not start <= durable_start <= durable_end <= end or
                durable_end - durable_start > 16368 or
                symbols.get(b"tiku_layout_pin") != durable_start or
                (retained_start, statics_end) not in allocated):
            raise ValueError("firmware retained or durable image bounds disagree")
    if (not RAM_START <= start < end <= KERNEL_LOW_END - (0 if firmware else 16384) or end % 16 or
            statics_end != low_end or symbols.get(b"__tier_sram_extra_start") != statics_end or
            not any(s[1] == 8 and s[3] == start and s[5] == end - start
                    for s in sections) or
            any(base + len(payload) > start for base, payload in segments) or
            (guard_address, KERNEL_STACK - stack_size) not in allocated or
            (KERNEL_STACK - stack_size, KERNEL_STACK) not in allocated):
        raise ValueError("kernel statics, free span or stack reservation mismatch")
    guard = symbols.get(b"tiku_p4_isr_guard", 0)
    if (not start <= guard <= end - 64 - 2048 or guard % 16 or
            symbols.get(b"tiku_p4_isr_stack") != guard + 64 or
            symbols.get(b"tiku_p4_isr_stack_end") != guard + 64 + 2048):
        raise ValueError("ISR stack/guard outside BSS")
    return {"high": guard_address - KERNEL_HIGH_START,
            "low": KERNEL_LOW_END - statics_end, "statics": statics_end - RAM_START}


def require_port(port, serial_number, ports):
    """Refuse a missing or different USB serial number before resetting it."""
    if not port or not serial_number:
        raise ValueError("explicit --port and --serial are required")
    if not any(os.path.realpath(p.device) == os.path.realpath(port) and
               (p.serial_number or "").upper() == serial_number.upper()
               for p in ports):
        raise ValueError("USB serial number does not match the requested board")
    require_idle_usb_port(port, ports)


def validate_target(esp):
    """Require the measured v1.3/ECO2 unsecured P4 ROM, never a stub."""
    if (esp.CHIP_NAME != "ESP32-P4" or esp.IS_STUB or
            esp.secure_download_mode):
        raise ValueError("requires unsecured ESP32-P4 ROM loader")
    if (esp.read_reg(0x4FC00010) != CHIP_ID or
            esp.read_reg(0x4FC00014) != ROM_ECO or
            esp.get_major_chip_version() * 100 +
            esp.get_minor_chip_version() != REVISION):
        raise ValueError("only P4 silicon v1.3 and ROM ECO2 are supported")
    security = esp.get_security_info()
    if (security["chip_id"] != CHIP_ID or security["flags"] != 0 or
            security["flash_crypt_cnt"] != 0):
        raise ValueError("unexpected security configuration; refusing RAM load")


def counters(data):
    """Extract complete tick, IRQ, timer-error and unclaimed-IRQ records."""
    return [tuple(int(value, 16) for value in row) for row in re.findall(
        rb"ticks=0x([0-9a-f]{8}) interrupts=0x([0-9a-f]{8}) "
        rb"timer_errors=0x([0-9a-f]{8}) unclaimed=0x([0-9a-f]{8})\r?\n",
        data)]


def validate_capture(data):
    """Require complete positive diagnostic evidence without faults/resets."""
    required = (b"TikuOS ESP32-P4 RAM diagnostic; ROM ECO=0x00000002",
                b"PASS: data, bss, SRAM read/write\r\n", b"PONG\r\n",
                b"PASS: nested IRQ mask and delayed ticks\r\n",
                b"PASS: IRQ register frame\r\n",
                b"PASS: one-shot alarm rearm and cancel\r\n",
                b"PASS: watchdog kick, pause, resume, off\r\n")
    rows = counters(data)
    beats = [int(n, 16) for n in re.findall(
        rb"heartbeat=0x([0-9a-f]{8})\r?\n", data)]
    clocks = re.findall(rb"cpu_MHz=0x([0-9a-f]{8})", data)
    if (any(marker not in data for marker in required) or
            any(marker in data for marker in (b"FAIL:", b"TRAP ", b"ESP-ROM:")) or
            len(rows) < 3 or len(beats) != len(rows) or
            len(clocks) != len(beats) or any(c != b"00000168" for c in clocks) or
            beats[-1] <= beats[0] or any(b < a for a, b in zip(beats, beats[1:])) or
            any(row[2] or row[3] for row in rows) or
            rows[-1][0] <= rows[0][0] or rows[-1][1] <= rows[0][1] or
            any(b[0] < a[0] or b[1] < a[1] for a, b in zip(rows, rows[1:]))):
        raise ValueError("P4 diagnostic incomplete, stalled, reset or failed")
    rates = (40, 90, 180, 360, 180, 90, 40, 360)
    changes = [tuple(int(value, 16) for value in row) for row in re.findall(
        rb"PASS: clock requested=0x([0-9a-f]{8}) measured=0x([0-9a-f]{8}) "
        rb"IRQs=0x([0-9a-f]{8})\r?\n", data)]
    if (len(changes) != len(rates) or
            any(want != rates[i] or measured != want or not 5 <= irqs <= 8
                for i, (want, measured, irqs) in enumerate(changes))):
        raise ValueError("P4 frequency transitions or live IRQ checks failed")
    return rows


def validate_continuity(previous, current):
    """Reject a counter restart between two separate console captures."""
    if (current[0][0] < previous[-1][0] or
            current[0][1] < previous[-1][1] or
            current[-1][0] <= previous[-1][0] or
            current[-1][1] <= previous[-1][1]):
        raise ValueError("P4 reset or stalled across console reopen")


KERNEL_CHECKS = (
    "process start", "event enqueue", "event and coalesced poll", "process exit",
    "software timer dispatch", "software timer cancellation",
    "nested atomic masks tick", "atomic exit restores tick", "worker start",
    "double start refused", "hardware timer arm", "worker join",
    "worker integer state", "worker and kernel errno",
    "interrupt exercised worker libc state",
    "interrupt delivery and exception guard", "foreground exception guard",
    "reused worker errno reset", "register probe workers joined",
    "preempted worker register frames", "spinners start", "tick preempts both spinners",
    "kernel timer serviced during worker load", "worker budget exhausted",
    "exhausted worker not scheduled", "worker budget refill",
    "spinners stopped and joined", "timed wait start", "wait timeout",
    "queue wait start", "worker blocks on queue", "queue wake and cleanup",
    "stack canaries and cycle accounting", "worker slot release",
    "hardware timer cancel", "cancelled hardware timer stays quiet",
    "masked critical window", "critical window masks tick",
    "critical window restores tick", "libc heap growth refused",
    "scheduler WFI wakes and dispatches", "allocator refuses IRQ and worker mutation",
    "L2 partition agrees with SRAM map",
    "two SRAM allocator spans", "SRAM linker and telemetry agree",
    "ROM cache TCM and LP SRAM excluded",
    "absent NVM PSRAM and hardware protection reported",
    "aggregate free cannot cross SRAM gap", "entire low and high SRAM reserved",
    "full SRAM patterns and span alias check",
    "allocator initialization preserves live reservations",
    "full SRAM reservations returned", "aligned arena and live object refusal",
    "aligned pool creation", "pool blocks and live block refusal",
    "pool backing returned after block release", "workspace backing returned",
    *(
        "three reservations in selected span",
        "non LIFO release coalesces and rejects stale handles",
        "reclaim owner registration", "reclaim fragmented placement",
        "fragmented span refuses oversized gap", "reclaim request submitted",
        "restore failure preserves fence", "reclaim recovery retry",
        "reclaim restores owner and invalidates stale handle",
        "reclaim held capacity accounted", "reclaim releases all reservations",
    ) * 2,
    "reservation metadata exhaustion", "bounded secure wipe",
    "both stack guard corruptions detected", "all SRAM capacity restored",
    "timer IRQ and console health",
)


def validate_kernel_capture(data):
    """Require the ordered RAM-kernel assertions and advancing timer IRQs."""
    grammar = (rb"P4K (?:BEGIN [0-9a-f]{8}|(?:PASS|FAIL) [0-9a-f]{8} [^\r\n]+|"
               rb"END total=[0-9a-f]{8} fail=[0-9a-f]{8} run=[0-9a-f]{8}|"
               rb"LIVE ticks=[0-9a-f]{8} irqs=[0-9a-f]{8})")
    if any(b"P4K" in line and not re.fullmatch(grammar, line)
           for line in data.splitlines()):
        raise ValueError("malformed P4 kernel record")
    begins = re.findall(rb"P4K BEGIN ([0-9a-f]{8})\r?\n", data)
    ends = re.findall(rb"P4K END total=([0-9a-f]{8}) fail=([0-9a-f]{8}) "
                      rb"run=([0-9a-f]{8})\r?\n", data)
    checks = re.findall(rb"P4K PASS ([0-9a-f]{8}) ([^\r\n]+)\r?\n", data)
    rows = [tuple(int(v, 16) for v in row) for row in re.findall(
        rb"P4K LIVE ticks=([0-9a-f]{8}) irqs=([0-9a-f]{8})\r?\n", data)]
    expected = [(f"{i:08x}".encode(), name.encode())
                for i, name in enumerate(KERNEL_CHECKS, 1)]
    if (len(begins) != 1 or len(ends) != 1 or checks != expected or
            any(marker in data for marker in (b"FAIL", b"TRAP", b"ESP-ROM:")) or
            len(rows) < 3 or any(b[0] <= a[0] or b[1] <= a[1]
                                  for a, b in zip(rows, rows[1:]))):
        raise ValueError("P4 kernel tests incomplete, stalled, reset or failed")
    total, failures, run = (int(v, 16) for v in ends[0])
    if total != len(expected) or failures or not run or run != int(begins[0], 16):
        raise ValueError("P4 kernel summary mismatch")
    if not data.index(b"P4K BEGIN ") < data.index(b"P4K PASS ") < data.index(b"P4K END "):
        raise ValueError("P4 kernel marker order")
    if data.rindex(b"P4K PASS ") > data.index(b"P4K END "):
        raise ValueError("P4 kernel assertion outside test run")
    return [(*row, run) for row in rows]


def capture(wire, seconds, log, kernel_test=False):
    """Capture bounded output and explicitly request each diagnostic check."""
    data = bytearray()
    end = time.monotonic() + seconds
    query_at = time.monotonic() + 0.25
    wire.timeout = 0.1
    while time.monotonic() < end:
        if time.monotonic() >= query_at:
            wire.write(b"k" if kernel_test else b"ipcrfaw")
            query_at = end + 1
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 1024 * 1024:
            raise ValueError("diagnostic output exceeds 1 MiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log is not None:
                log.write(chunk)
                log.flush()
    return validate_kernel_capture(data) if kernel_test else validate_capture(data)


FAULT_FIELDS = ("version", "sequence", "cause", "pc", "value", "sp", "status",
                "hart", "reset", "ra", "flags", "crc")
FAULT_CASES = ((b"t", 3, 0), (b"x", 2, 0), (b"v", 5, 0),
               (b"b", 3, 1), (b"n", 3, 2))


def fault_bytes(record):
    """Encode the exact 64-byte firmware record for ROM/CRC comparison."""
    return struct.pack("<16I", 0x50344654,
                       *(record[name] for name in FAULT_FIELDS[:-1]),
                       0, 0, 0, record["crc"])


def validate_fault(data, cause=3, flags=0, kernel_test=False, prefix=b"TRAP"):
    """Require one complete, checksummed record with the expected exception context."""
    pattern = prefix + b"".join(b" " + name.encode() + rb"=0x([0-9a-f]{8})"
                                for name in FAULT_FIELDS) + rb"\r?\n"
    rows = re.findall(pattern, data)
    if len(rows) != 1 or data.count(prefix + b" ") != 1:
        raise ValueError("missing, duplicate or malformed fault report")
    record = dict(zip(FAULT_FIELDS, (int(value, 16) for value in rows[0])))
    pc, sp = record["pc"], record["sp"]
    limit = KERNEL_LOW_END if kernel_test else RAM_LIMIT
    stack_top = KERNEL_STACK if kernel_test else RAM_LIMIT + 4096
    valid_sp = (sp == 16 if flags & 1 else sp % 16 == 0 and
                (RAM_START + 144 <= sp < limit or stack_top - 4096 + 144 <= sp <= stack_top))
    if (record["cause"] & 0x80000FFF != cause or not RAM_START <= pc < limit or
            pc % 2 or record["flags"] != flags or not valid_sp or
            record["hart"] != 0 or record["version"] != 1 or not record["sequence"] or
            record["status"] & 8 or record["crc"] != zlib.crc32(fault_bytes(record)[4:60]) or
            b"FAIL:" in data or b"ESP-ROM:" in data):
        raise ValueError("unexpected fault context, checksum or reset")
    return record


def capture_command(wire, log, command, seconds=2):
    """Capture one explicit diagnostic command with a 64 KiB output bound."""
    wire.reset_input_buffer()
    wire.write(command)
    wire.timeout = 0.1
    data = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("fault output exceeds 64 KiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log:
                log.write(chunk)
                log.flush()
    return data


def check_faults(wire, log, esptool, image, seconds, kernel_test):
    """Inject five exceptions, verify retained bytes in ROM, then reload and query."""
    capture_command(wire, log, b"d")
    validate_fault_clear(capture_command(wire, log, b"D"))
    for sequence, (command, cause, flags) in enumerate(FAULT_CASES, 1):
        record = validate_fault(capture_command(wire, log, command),
                                cause, flags, kernel_test)
        if record["sequence"] != sequence:
            raise ValueError("fault record sequence did not advance")
        esp = connect_rom(esptool, wire)
        validate_target(esp)
        stored = struct.pack("<16I", *(esp.read_reg(FAULT_START + offset)
                                       for offset in range(0, FAULT_SIZE, 4)))
        if stored != fault_bytes(record):
            raise ValueError("fault record did not survive ROM reset")
        esptool.load_ram(esp, image)
        capture(wire, seconds, log, kernel_test)
        replay = validate_fault(capture_command(wire, log, b"d"), cause, flags,
                                kernel_test, b"P4FAULT")
        if replay != record:
            raise ValueError("fault record changed across RAM reload")
        line = f"PASS: fault {command.decode()} retained across ROM reset and RAM reload\n"
        print(line, end="", flush=True)
        if log:
            log.write(line.encode()); log.flush()
    validate_fault_clear(capture_command(wire, log, b"D"))


def validate_fault_clear(data):
    """Require a single absent-record response without a reset or terminal fault."""
    if (data.count(b"P4FAULT ") != 1 or b"P4FAULT NONE\r\n" not in data or
            any(marker in data for marker in (b"TRAP ", b"FAIL:", b"ESP-ROM:"))):
        raise ValueError("fault record did not clear")


def validate_watchdog_reset(esp, data):
    """Require the requested diagnostic and HP watchdog system-reset cause."""
    if (b"WATCHDOG: expiring into ROM download mode\r\n" not in data or
            b"FAIL:" in data or b"TRAP " in data or
            (esp.read_reg(0x50111010) >> 7) & 63 != 7):
        raise ValueError("watchdog reset was not confirmed")


def capture_watchdog(wire, log):
    """Request a watchdog reset and capture bounded output without resetting USB."""
    wire.reset_input_buffer()
    wire.write(b"W")
    wire.timeout = 0.1
    data = bytearray()
    end = time.monotonic() + 2
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("watchdog output exceeds 64 KiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log:
                log.write(chunk)
                log.flush()
    return data


def flash_reference(path):
    """Load a complete flash backup before opening a port; retain only hashes."""
    with path.open("rb") as stream:
        data = stream.read(FLASH_SIZE + 1)
    if len(data) != FLASH_SIZE:
        raise ValueError("flash reference must be exactly 16 MiB")
    return {"sha256": hashlib.sha256(data).hexdigest(),
            "md5": hashlib.md5(data).hexdigest(),
            "crc": zlib.crc32(data),
            "blocks": [zlib.crc32(data[i:i + FLASH_BLOCK])
                       for i in range(0, FLASH_SIZE, FLASH_BLOCK)],
            "clock_crc": zlib.crc32(data[0x3fe01:0x40201])}


def validate_flash_capture(data, reference):
    """Require the entire ordered flash scan, backup CRCs and IRQ progress."""
    if any(marker in data for marker in (b"FAIL:", b"TRAP", b"ESP-ROM:", b"P4FAULT")):
        raise ValueError("flash scan fault, reset or failed assertion")
    lines = [line for line in data.splitlines() if line.startswith(b"P4FLASH")]
    if len(lines) != 72 or lines[0] != b"P4FLASH BEGIN" or lines[2] != b"P4FLASH PASS bounds":
        raise ValueError("incomplete or duplicate flash scan")
    identity = re.fullmatch(rb"P4FLASH ID jedec=001840ef size=01000000 status=([0-9a-f]{8})", lines[1])
    if not identity or int(identity[1], 16) > 0xffff or int(identity[1], 16) & 3:
        raise ValueError("flash identity, size or write/busy status")
    for index, crc in enumerate(reference["blocks"]):
        expected = f"P4FLASH BLOCK offset={index * FLASH_BLOCK:08x} crc={crc:08x}".encode()
        if lines[3 + index] != expected:
            raise ValueError(f"flash block {index} differs from backup or is out of order")
    for index, mhz in enumerate((40, 90, 180, 360)):
        expected = f"P4FLASH CLOCK mhz={mhz:08x} crc={reference['clock_crc']:08x}".encode()
        if lines[67 + index] != expected:
            raise ValueError("flash read differs at a CPU clock setting")
    end = re.fullmatch(rb"P4FLASH END crc=([0-9a-f]{8}) ticks=([0-9a-f]{8}) "
                       rb"irqs=([0-9a-f]{8}) status=([0-9a-f]{8})", lines[71])
    if (not end or int(end[1], 16) != reference["crc"] or end[4] != identity[1] or
            not 128 <= int(end[3], 16) <= int(end[2], 16) < 128 * 120):
        raise ValueError("flash checksum, unchanged status or timer progress missing")
    if not re.search(rb"(?m)^P4FLASH END [^\r\n]*\r?\n", data):
        raise ValueError("truncated flash record")


def check_flash_rom(esp, reference, log):
    """Compare the complete chip through ROM without programming or unlocking."""
    esp.flash_spi_attach(0)
    esp.flash_set_parameters(FLASH_SIZE)
    if esp.flash_id() != 0x1840ef:
        raise ValueError("ROM flash identity differs from qualified board")
    if esp.flash_md5sum(0, FLASH_SIZE).lower() != reference["md5"]:
        raise ValueError("ROM flash digest differs from reference backup")
    record = f"PASS: ROM full 16 MiB digest; backup-sha256={reference['sha256']}\n"
    print(record, end="", flush=True)
    if log:
        log.write(record.encode())
        log.flush()


def capture_flash(wire, log, reference, mapped=False):
    """Request one read-only scan and reject missing completion within 90 s."""
    wire.timeout = 0.1
    wire.write(b"M" if mapped else b"F")
    data = bytearray()
    end = time.monotonic() + 90
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("flash scan output exceeds 64 KiB")
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if log:
                log.write(chunk)
                log.flush()
        complete = b"P4MAP END\r\n" in data if mapped else re.search(rb"P4FLASH END [^\r\n]*\r\n", data)
        if b"FAIL:" in data or complete:
            break
    validate_flash_capture(data, reference)
    if mapped and [line for line in data.splitlines() if line.startswith(b"P4MAP")] != [
            b"P4MAP BEGIN", b"P4MAP READY", b"P4MAP END"]:
        raise ValueError("mapped flash initialization or shutdown is incomplete")


def require_psram_image(meta):
    """Require exactly one PSRAM-test marker in the validated loaded segments."""
    if sum(p.count(PSRAM_TEST_MARKER) for _, p in meta["segments"]) != 1:
        raise ValueError("--psram-check requires the explicit PSRAM qualification image")


def validate_psram_capture(data):
    """Require complete full-capacity patterns and allocator checks, without reset."""
    rows = [line for line in data.splitlines() if line.startswith(b"P4PS")]
    expected = [b"P4PS BEGIN", b"P4PS PASS init"] + [
        b"P4PS PASS full 32 MiB pattern with IRQs"] * 4 + [
        b"P4PS PASS narrow boundary access", b"P4PS PASS allocator lifecycle",
        b"P4PS PASS whole capacity and forced detach", b"P4PS END"]
    if (rows != expected or data.count(PSRAM_TEST_MARKER + b"\r\n") != 1 or
            b"P4PS END\r\n" not in data or any(x in data for x in
            (b"FAIL", b"TRAP ", b"ESP-ROM:", b"psram_bad_offset"))):
        raise ValueError("PSRAM qualification is incomplete or failed")


def capture_psram(wire, log):
    """Run the explicit destructive, unallocated-PSRAM test with bounded output."""
    wire.write(b"P")
    data = bytearray()
    end = time.monotonic() + 60
    while time.monotonic() < end:
        chunk = wire.read(4096)
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("PSRAM output exceeds 64 KiB")
        if chunk:
            print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
            if log:
                log.write(chunk); log.flush()
        if any(x in data for x in (b"P4PS END\r\n", b"FAIL", b"TRAP ", b"ESP-ROM:")):
            break
    validate_psram_capture(data)


def main():
    """Validate before opening USB; ROM RAM download never programs flash."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--kernel-test", action="store_true",
                        help="request the storage-free CPU0 kernel suite")
    parser.add_argument("--port")
    parser.add_argument("--serial")
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--reopen", type=int, default=2)
    parser.add_argument("--fault-check", action="store_true",
                        help="inject five faults; reset/reload RAM and verify retained records")
    parser.add_argument("--no-reset", action="store_true",
                        help="connect to an already waiting ROM loader without resetting")
    parser.add_argument("--watchdog-check", action="store_true",
                        help="expire watchdog into ROM, verify cause without reset, reload")
    parser.add_argument("--flash-check", type=Path, metavar="BACKUP",
                        help="compare read-only full flash scan and ROM digests with a 16 MiB backup")
    parser.add_argument("--mmap-check", action="store_true",
                        help="use SPI0 mapped reads for --flash-check; requires --kernel-test")
    parser.add_argument("--psram-check", action="store_true",
                        help="destructively test unallocated PSRAM; requires P4_PSRAM_TEST=1")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    try:
        if args.kernel_test and args.watchdog_check:
            raise ValueError("watchdog reset checks require the diagnostic profile")
        if args.mmap_check and not (args.kernel_test and args.flash_check):
            raise ValueError("--mmap-check requires --kernel-test and --flash-check")
        if args.psram_check and not args.kernel_test:
            raise ValueError("--psram-check requires --kernel-test")
        if not 5 <= args.seconds <= 30 or not 0 <= args.reopen <= 10:
            raise ValueError("seconds must be 5..30; reopen must be 0..10")
        reference = flash_reference(args.flash_check) if args.flash_check else None
        with args.image.open("rb") as stream:
            data = stream.read(KERNEL_LOW_END - RAM_START + 257)
        meta = validate_image(data, args.kernel_test)
        if has_firmware(meta):
            raise ValueError("normal firmware requires tools/esp32p4_firmware.py and a backup")
        if has_scratch_writer(meta):
            raise ValueError("scratch-write image requires tools/esp32p4_flash_test.py")
        if has_storage_writer(meta):
            raise ValueError("storage-write image requires tools/esp32p4_storage_test.py")
        if args.psram_check:
            require_psram_image(meta)
        with args.elf.open("rb") as stream:
            elf = stream.read(4 * 1024 * 1024 + 1)
        if len(elf) > 4 * 1024 * 1024:
            raise ValueError("ELF exceeds diagnostic size bound")
        layout = validate_elf(elf, meta)
        if layout:
            print(f"P4 SRAM: statics={layout['statics']} primary={layout['high']} "
                  f"extra={layout['low']} bytes; stack=4096+64 guard; "
                  "ISR=2048+64 guard, fault-stack=2048+32 scratch inside BSS; "
                  "LP-fault-record=64; L2=131072 reserved")
        if args.check_only:
            print("PASS: P4 v1.3 image, SHA-256, ELF ABI and SRAM bounds")
            return 0
        import esptool
        import serial
        from serial.tools import list_ports
        if esptool.__version__ != "5.1.0":
            raise ValueError("requires esptool==5.1.0")
        require_port(args.port, args.serial, list(list_ports.comports()))
        log = args.log.open("xb") if args.log else None
        try:
            with usb_console(serial, args.port) as wire:
                esp = connect_rom(esptool, wire, args.no_reset)
                validate_target(esp)
                record = (f"P4 ROM verified; no-reset={args.no_reset}; "
                          f"root=0x{esp.read_reg(0x50111040):08x}; "
                          f"div0=0x{esp.read_reg(0x500e6004):08x}; "
                          f"image-sha256={hashlib.sha256(data).hexdigest()}\n")
                print(record, end="", flush=True)
                if log:
                    log.write(record.encode())
                    log.flush()
                if reference:
                    check_flash_rom(esp, reference, log)
                esptool.load_ram(esp, data)
                previous = capture(wire, args.seconds, log, args.kernel_test)
            for _ in range(args.reopen):
                require_port(args.port, args.serial, list(list_ports.comports()))
                with usb_console(serial, args.port) as wire:
                    current = capture(wire, args.seconds, log, args.kernel_test)
                validate_continuity(previous, current)
                if args.kernel_test and current[0][2] != previous[-1][2] + 1:
                    raise ValueError("P4 kernel run sequence changed across reopen")
                previous = current
            if reference:
                require_port(args.port, args.serial, list(list_ports.comports()))
                with usb_console(serial, args.port) as wire:
                    capture_flash(wire, log, reference, args.mmap_check)
                    current = capture(wire, args.seconds, log, args.kernel_test)
                    validate_continuity(previous, current)
                    if args.kernel_test and current[0][2] != previous[-1][2] + 1:
                        raise ValueError("kernel restarted during flash scan")
                    esp = connect_rom(esptool, wire)
                    validate_target(esp)
                    check_flash_rom(esp, reference, log)
                    esptool.load_ram(esp, data)
                    previous = capture(wire, args.seconds, log, args.kernel_test)
            if args.psram_check:
                require_port(args.port, args.serial, list(list_ports.comports()))
                with usb_console(serial, args.port) as wire:
                    capture_psram(wire, log)
                    current = capture(wire, args.seconds, log, True)
                    validate_continuity(previous, current)
                    if current[0][2] != previous[-1][2] + 1:
                        raise ValueError("kernel restarted during PSRAM qualification")
            if args.fault_check:
                require_port(args.port, args.serial, list(list_ports.comports()))
                with usb_console(serial, args.port) as wire:
                    check_faults(wire, log, esptool, data, args.seconds, args.kernel_test)
            if args.watchdog_check:
                require_port(args.port, args.serial, list(list_ports.comports()))
                with usb_console(serial, args.port) as wire:
                    reset_data = capture_watchdog(wire, log)
                    esp = connect_rom(esptool, wire, True)
                    validate_target(esp)
                    validate_watchdog_reset(esp, reset_data)
                    record = "PASS: watchdog reset cause=7; ROM connected without reset\n"
                    print(record, end="", flush=True)
                    if log:
                        log.write(record.encode())
                        log.flush()
                    esptool.load_ram(esp, data)
                    capture(wire, args.seconds, log)
        finally:
            if log:
                log.close()
        print("PASS: native P4 RAM " + ("kernel tests" if args.kernel_test else
              "diagnostic") + "; flash was not written")
        return 0
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


def connect_rom(esptool, wire, no_reset=False):
    """Use no-reset without fallback; physical power removal is a user action."""
    if no_reset:
        return esptool.detect_chip(wire, connect_mode="no-reset", connect_attempts=1)
    return esptool.detect_chip(wire, connect_attempts=3)


if __name__ == "__main__":
    sys.exit(main())
