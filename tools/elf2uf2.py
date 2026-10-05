#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

elf2uf2.py - convert a raw RP2350 flash image into a UF2 file.

Reads the objcopy -O binary image (main.bin) and writes the UF2 file the
BOOTSEL ROM accepts, with the RP2350 family ID 0xe48bff59 from microsoft/uf2.

SPDX-License-Identifier: Apache-2.0

Usage:
  python3 tools/elf2uf2.py main.bin main.uf2

The UF2 spec is one 512-byte block per 256 bytes of payload:

  uint32  magicStart0 = 0x0A324655 ("UF2\\n")
  uint32  magicStart1 = 0x9E5D5157
  uint32  flags       = 0x00002000   (familyID present)
  uint32  targetAddr  = absolute flash address
  uint32  payloadSize = always 256
  uint32  blockNo
  uint32  numBlocks
  uint32  fileSize    = familyID for this flag
  uint8[476] data     (first 256 bytes meaningful, rest padding)
  uint32  magicEnd    = 0x0AB16F30

Reference: https://github.com/microsoft/uf2/blob/master/README.md
"""

import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000

RP2350_FAMILY_ID = 0xE48BFF59

# Start of the XIP flash window.  The image is written from here, where the
# boot ROM finds the .boot2 stub and the IMAGE_DEF block.
FLASH_BASE = 0x10000000

PAYLOAD_SIZE = 256


def usage_exit():
    """Print the usage line and exit with status 2."""
    print("Usage: elf2uf2.py <flash.bin> <out.uf2>", file=sys.stderr)
    sys.exit(2)


def main(argv):
    """Write argv[2] from argv[1]: one UF2 block per 256 bytes, zero-padded."""
    if len(argv) != 3:
        usage_exit()
    in_path, out_path = argv[1], argv[2]

    with open(in_path, "rb") as f:
        data = f.read()
    if not data:
        print("error: empty input", file=sys.stderr)
        sys.exit(1)

    # Pad to a multiple of PAYLOAD_SIZE.
    pad = (-len(data)) % PAYLOAD_SIZE
    if pad:
        data += b"\x00" * pad

    n_blocks = len(data) // PAYLOAD_SIZE

    with open(out_path, "wb") as f:
        for i in range(n_blocks):
            chunk = data[i * PAYLOAD_SIZE:(i + 1) * PAYLOAD_SIZE]
            target = FLASH_BASE + i * PAYLOAD_SIZE
            block = bytearray(512)
            struct.pack_into(
                "<IIIIIIII", block, 0,
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY,
                target,
                PAYLOAD_SIZE,
                i,
                n_blocks,
                RP2350_FAMILY_ID,
            )
            block[32:32 + PAYLOAD_SIZE] = chunk
            struct.pack_into("<I", block, 508, UF2_MAGIC_END)
            f.write(block)

    print(f"  wrote {n_blocks} block(s), {n_blocks * 512} bytes -> {out_path}")


if __name__ == "__main__":
    main(sys.argv)
