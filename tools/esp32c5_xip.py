#!/usr/bin/env python3
# Tiku Operating System v0.06
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# esp32c5_xip.py - pack and validate paired C5 SRAM boot and flash code images.
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

import esp32c5_ram as ram

MAGIC = b"TIKUC5XP"
HEADER_SIZE = 64
XIP_ADDRESS = 0x42100000
XIP_OFFSET = 0x100000
XIP_CAPACITY = 0x100000
PLACEHOLDER = MAGIC + struct.pack("<I", 1) + bytes(52)


def segments(boot):
    """Yield validated ROM segment addresses, data offsets and lengths."""
    ram.validate_image(boot, "kernel")
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
    digest = hashlib.sha256(b"TikuOS ESP32-C5 paired image v1\0" + boot[4:8])
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
    ram.validate_image(patched, "kernel")
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


def inspect_elf(data):
    """Validate ELF32 section placement and critical SRAM symbols before splitting."""
    if (len(data) < 52 or len(data) > 32 * 1024 * 1024 or
            data[:7] != b"\x7fELF\x01\x01\x01" or
            struct.unpack_from("<HH", data, 16) != (2, 243)):
        raise ValueError("expected a linked little-endian RISC-V ELF32")
    table = struct.unpack_from("<I", data, 32)[0]
    size, count, strings = struct.unpack_from("<HHH", data, 46)
    if size != 40 or not 1 <= count <= 4096 or strings >= count or table + count * size > len(data):
        raise ValueError("invalid ELF section table")
    sections = [struct.unpack_from("<10I", data, table + i * size) for i in range(count)]

    def contents(section):
        """Return one bounded, file-backed ELF section."""
        start, length = section[4:6]
        if start + length > len(data):
            raise ValueError("truncated ELF section")
        return data[start:start + length]

    def name(pool, offset):
        """Resolve a terminated ASCII name from its string table."""
        if offset >= len(pool) or pool.find(b"\0", offset) < 0:
            raise ValueError("invalid ELF string offset")
        return pool[offset:pool.index(b"\0", offset)].decode("ascii")

    names = contents(sections[strings])
    symbols, found = {}, {}
    for section in sections:
        label = name(names, section[0])
        kind, flags, address, _, length = section[1:6]
        if flags & 2 and length:
            if label == ".xip":
                if address != XIP_ADDRESS or not HEADER_SIZE < length <= XIP_CAPACITY or flags & 1:
                    raise ValueError("invalid executable XIP section")
            elif not ram.RAM_START <= address < address + length <= 0x4084E5A0:
                raise ValueError(f"{label} is outside internal SRAM")
            found[label] = section
        if kind == 2:
            if section[9] != 16 or length % 16 or section[6] >= count:
                raise ValueError("invalid ELF symbol table")
            pool = contents(sections[section[6]])
            for entry in struct.iter_unpack("<IIIBBH", contents(section)):
                if entry[5] and entry[0]:
                    symbols[name(pool, entry[0])] = entry[1]
    if ".xip" not in found or ".c5.expected" not in found or found[".c5.expected"][5] != HEADER_SIZE:
        raise ValueError("missing XIP section or SRAM descriptor")
    required = ("tiku_esp32c5_reset_handler", "tiku_esp32c5_diagnostic_trap",
                "tiku_c5_trap_dispatch", "tiku_c5_fatal", "tiku_cpu_boot_init",
                "tiku_c5_xip_require", "tiku_c5_xip_validate", "tiku_flash_init",
                "tiku_flash_erase_sector", "tiku_flash_program", "tiku_c5_xip_expected",
                "tiku_c5_isr_stack", "tiku_c5_isr_stack_end", "__stack_bottom", "__stack")
    for symbol in required:
        if not ram.RAM_START <= symbols.get(symbol, 0) <= 0x4084E5A0:
            raise ValueError(f"required internal SRAM symbol missing or misplaced: {symbol}")
    for symbol, address in symbols.items():
        if symbol.startswith("tiku_c5_psram_") and not ram.RAM_START <= address < 0x4084E5A0:
            raise ValueError(f"PSRAM lifecycle must execute from internal SRAM: {symbol}")
    return contents(found[".xip"])


def main():
    """Create the paired images and a patched ELF after internal-residency checks."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--xip", type=Path, required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--esptool", required=True)
    args = parser.parse_args()
    xip = inspect_elf(args.elf.read_bytes())
    with tempfile.TemporaryDirectory(prefix="c5-pair-", dir=args.elf.parent) as tmp:
        root = Path(tmp)
        sram, raw = root / "sram.elf", root / "boot.bin"
        subprocess.run([args.objcopy, "-R", ".xip", str(args.elf), str(sram)], check=True)
        subprocess.run([args.esptool, "--chip", "esp32c5", "elf2image", "--flash-mode", "dio",
                        "--flash-size", "4MB", "--min-rev-full", "100",
                        "--max-rev-full", "199", "-o", str(raw), str(sram)],
                       check=True)
        boot, xip = pack_pair(raw.read_bytes(), xip)
        offset = descriptor_offset(boot)
        (root / "header.bin").write_bytes(boot[offset:offset + HEADER_SIZE])
        (root / "xip.bin").write_bytes(xip)
        subprocess.run([args.objcopy, "--update-section", f".c5.expected={root / 'header.bin'}",
                        "--update-section", f".xip={root / 'xip.bin'}", str(args.elf),
                        str(args.elf.with_suffix(".paired.elf"))], check=True)
        args.boot.write_bytes(boot)
        args.xip.write_bytes(xip)
    print(f"C5 paired images: boot {len(boot)} bytes, XIP {len(xip)} bytes; SRAM residency checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
