#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32p4_layout.py - Read the P4's shared physical flash range definitions.
#
# Parses arch/esp32p4/flash_layout.def into LAYOUT, name -> (start, end)
# with end exclusive.  Import fails unless the ranges tile 16 MiB in the
# fixed order.
#
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import re


def parse(text):
    """Require the complete ordered, contiguous, sector-aligned 16 MiB layout."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    ranges = {}
    end = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"TIKU_P4_FLASH_RANGE\(([A-Z_]+), (0x[0-9a-f]+), (0x[0-9a-f]+)\)", line)
        if not match or match[1] in ranges:
            raise ValueError("invalid or duplicate P4 flash range")
        start, stop = int(match[2], 16), int(match[3], 16)
        if start != end or stop <= start or stop % 4096:
            raise ValueError("P4 flash ranges must be contiguous and sector aligned")
        ranges[match[1]] = (start, stop)
        end = stop
    if list(ranges) != ["RESERVED", "BOOT", "XIP", "REGION", "MIRROR_A", "MIRROR_B", "SCRATCH", "TAIL"] or end != 0x1000000:
        raise ValueError("incomplete P4 flash layout")
    if ranges["BOOT"][0] != 0x2000 or ranges["SCRATCH"][1] - ranges["SCRATCH"][0] != 4096:
        raise ValueError("invalid ROM boot or scratch extent")
    if any(ranges[key][1] - ranges[key][0] != 16384 for key in ("MIRROR_A", "MIRROR_B")):
        raise ValueError("mirror slots must each be 16 KiB")
    return ranges


LAYOUT = parse((Path(__file__).resolve().parents[1] / "arch/esp32p4/flash_layout.def").read_text())
