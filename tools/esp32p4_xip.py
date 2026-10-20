#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_xip.py - pack and check paired P4 SRAM boot and flash code images.
#
# Splits the ELF into an SRAM boot image and the .xip payload for flash
# offset 0x100000, binds them with a 64-byte descriptor in each and checks
# the SRAM layout before writing the .paired.elf.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

import esp32p4_ram as ram
from esp32p4_layout import LAYOUT

MAGIC = b"TIKUP4XP"
HEADER_SIZE = 64
XIP_OFFSET, XIP_END = LAYOUT['XIP']
XIP_ADDRESS = 0x40000000 + XIP_OFFSET
XIP_CAPACITY = XIP_END - XIP_OFFSET
PLACEHOLDER = MAGIC + struct.pack("<I", 1) + bytes(52)


def segments(boot):
    """Yield validated ROM segment addresses, data offsets and lengths."""
    ram.validate_image(boot, True)
    offset = 24
    for _ in range(boot[1]):
        address, length = struct.unpack_from("<II", boot, offset)
        yield address, offset + 8, length
        offset += 8 + length


def descriptor_offset(boot):
    """Find one aligned, complete SRAM descriptor, or return None for SRAM-only."""
    matches = []
    for address, offset, length in segments(boot):
        payload = boot[offset:offset + length]
        start = 0
        while True:
            at = payload.find(MAGIC, start)
            if at < 0:
                break
            if (address + at) % 4 or at + HEADER_SIZE > length:
                raise ValueError("unaligned or truncated XIP descriptor")
            matches.append(offset + at)
            start = at + 1
    if len(matches) > 1:
        raise ValueError("ambiguous XIP descriptor")
    return matches[0] if matches else None


def identity(boot, offset, payload):
    """Hash the entry, addressed SRAM segments with blank metadata, and XIP bytes."""
    digest = hashlib.sha256(b"TikuOS ESP32-P4 paired image v1\0" + boot[4:8])
    canonical = bytearray(boot)
    canonical[offset:offset + HEADER_SIZE] = PLACEHOLDER
    for address, start, length in segments(boot):
        digest.update(struct.pack("<II", address, length))
        digest.update(canonical[start:start + length])
    digest.update(payload)
    return digest.digest()


def with_descriptor(boot, offset, descriptor):
    """Replace metadata and regenerate the ROM image checksum and SHA-256."""
    patched = bytearray(boot)
    patched[offset:offset + HEADER_SIZE] = descriptor
    checksum = 0xEF
    for _, start, length in segments(boot):
        for value in patched[start:start + length]:
            checksum ^= value
    patched[-33] = checksum
    patched[-32:] = hashlib.sha256(patched[:-32]).digest()
    ram.validate_image(patched, True)
    return bytes(patched)


def pack_pair(boot, xip):
    """Fill both descriptors, binding the SRAM program and complete XIP payload."""
    offset = descriptor_offset(boot)
    if offset is None or boot[offset:offset + HEADER_SIZE] != PLACEHOLDER:
        raise ValueError("packer requires an unfilled SRAM XIP descriptor")
    if not HEADER_SIZE < len(xip) <= XIP_CAPACITY or len(xip) % 4:
        raise ValueError("XIP extent exceeds its partition or is empty/unaligned")
    if any(xip[:HEADER_SIZE]):
        raise ValueError("packer requires an unfilled XIP header")
    payload = xip[HEADER_SIZE:]
    header = (MAGIC + struct.pack("<III", 1, len(xip), zlib.crc32(payload)) +
              bytes(12) + identity(boot, offset, payload))
    paired_boot = with_descriptor(boot, offset, header)
    paired_xip = header + payload
    validate_pair(paired_boot, paired_xip)
    return paired_boot, paired_xip


def validate_pair(boot, xip=None):
    """Refuse missing, extra, mismatched, truncated or damaged companion images."""
    offset = descriptor_offset(boot)
    if offset is None:
        if xip is not None:
            raise ValueError("SRAM-only image does not accept an XIP companion")
        return
    if xip is None:
        raise ValueError("boot image requires its --xip companion")
    if not HEADER_SIZE < len(xip) <= XIP_CAPACITY or len(xip) % 4:
        raise ValueError("invalid XIP extent")
    header = boot[offset:offset + HEADER_SIZE]
    version, length, crc = struct.unpack_from("<III", header, 8)
    if (xip[:HEADER_SIZE] != header or version != 1 or length != len(xip) or
            any(header[20:32]) or zlib.crc32(xip[HEADER_SIZE:]) != crc or
            header[32:] != identity(boot, offset, xip[HEADER_SIZE:])):
        raise ValueError("XIP image does not match the SRAM program or payload digest")


def split_elf(data):
    """Extract one read-only XIP section and disable it in a copy for SRAM validation."""
    if (len(data) < 52 or len(data) > 4 * 1024 * 1024 or
            data[:7] != b"\x7fELF\x01\x01\x01"):
        raise ValueError("requires bounded ELF32")
    table = struct.unpack_from("<I", data, 32)[0]
    size, count, strings = struct.unpack_from("<HHH", data, 46)
    if size != 40 or not 1 <= count <= 4096 or strings >= count or table + count * size > len(data):
        raise ValueError("invalid ELF section table")
    sections = [struct.unpack_from("<10I", data, table + i * size) for i in range(count)]
    pool = sections[strings]
    if pool[1] != 3 or pool[4] + pool[5] > len(data):
        raise ValueError("invalid section name table")
    names = data[pool[4]:pool[4] + pool[5]]
    stripped, found = bytearray(data), None
    for i, section in enumerate(sections):
        name, kind, flags, address, offset, length = section[:6]
        if name >= len(names) or names.find(b"\0", name) < 0:
            raise ValueError("invalid section name")
        label = names[name:names.index(0, name)]
        if kind != 8 and offset + length > len(data):
            raise ValueError("truncated section")
        if label != b".xip":
            continue
        if length == HEADER_SIZE:
            raise ValueError("empty XIP section: the firmware profile links the "
                             "shell, BASIC, tests or applications to flash, and "
                             "this build has none of them")
        if (found is not None or kind != 1 or flags != 6 or address != XIP_ADDRESS or
                not HEADER_SIZE < length <= XIP_CAPACITY or length % 4):
            raise ValueError("invalid XIP section")
        found = bytes(data[offset:offset + length])
        struct.pack_into("<I", stripped, table + i * size + 8, 0)
    if found is None:
        raise ValueError("missing XIP section")
    return bytes(stripped), found


def validate_elf(data, boot, xip, firmware=False):
    """Validate the companion and every remaining allocated section with the SRAM guard."""
    validate_pair(boot, xip)
    stripped, payload = split_elf(data)
    if payload != xip:
        raise ValueError("ELF companion differs from image")
    meta = ram.validate_image(boot, True)
    meta["firmware"] = firmware
    return ram.validate_elf(stripped, meta)


def main():
    """Pack a paired image and check its ELF before publishing build artifacts."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--xip", type=Path, required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--esptool", required=True)
    parser.add_argument("--firmware", action="store_true")
    args = parser.parse_args()
    _, raw_xip = split_elf(args.elf.read_bytes())
    with tempfile.TemporaryDirectory(prefix="p4-pair-", dir=args.elf.parent) as tmp:
        root = Path(tmp)
        sram, raw = root / "sram.elf", root / "boot.bin"
        subprocess.run([args.objcopy, "-R", ".xip", str(args.elf), str(sram)], check=True)
        subprocess.run([args.esptool, "--chip", "esp32p4", "elf2image", "--flash-mode", "dio",
                        "--flash-freq", "40m", "--flash-size", "16MB",
                        "--min-rev-full", "103", "--max-rev-full", "103",
                        "-o", str(raw), str(sram)], check=True)
        meta = ram.validate_image(raw.read_bytes(), True)
        meta["firmware"] = args.firmware
        ram.validate_elf(sram.read_bytes(), meta)
        boot, xip = pack_pair(raw.read_bytes(), raw_xip)
        offset = descriptor_offset(boot)
        (root / "header.bin").write_bytes(boot[offset:offset + HEADER_SIZE])
        (root / "xip.bin").write_bytes(xip)
        paired = root / "paired.elf"
        subprocess.run([args.objcopy, "--update-section", f".p4.expected={root / 'header.bin'}",
                        "--update-section", f".xip={root / 'xip.bin'}", str(args.elf),
                        str(paired)], check=True)
        validate_elf(paired.read_bytes(), boot, xip, args.firmware)
        args.elf.with_suffix(".paired.elf").write_bytes(paired.read_bytes())
        args.boot.write_bytes(boot)
        args.xip.write_bytes(xip)
    print(f"P4 paired images: boot {len(boot)} bytes, XIP {len(xip)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
