#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
elf2uf2.py - Convert a flash binary to UF2 for the Raspberry Pi RP2350.

Usage:
  python3 tools/elf2uf2.py main.bin main.uf2

The input is a raw flash image (objcopy -O binary), the output is a
UF2 file the BOOTSEL ROM accepts. UF2 family ID for RP2350 is
0xe48bff59 (per microsoft/uf2).

The UF2 spec is one 512-byte block per 256 bytes of payload:

  uint32  magicStart0 = 0x0A324655 ("UF2\\n")
  uint32  magicStart1 = 0x9E5D5157
  uint32  flags       = 0x00002000   (familyID present)
  uint32  targetAddr  = absolute flash address
  uint32  payloadSize = always 256
  uint32  blockNo
  uint32  numBlocks
  uint32  fileSize    = familyID for our flag
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

# Pico 2 W flash starts here. Image must be loaded at the beginning
# of XIP, where the boot ROM expects to find the .boot2 + IMAGE_DEF
# blocks.
FLASH_BASE = 0x10000000

PAYLOAD_SIZE = 256


def usage_exit():
    print("Usage: elf2uf2.py <flash.bin> <out.uf2>", file=sys.stderr)
    sys.exit(2)


def main(argv):
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
