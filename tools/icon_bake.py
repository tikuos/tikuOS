#!/usr/bin/env python3
"""
icon_bake.py -- Convert PNG images into TikuOS gfx-kit image assets.

Takes a PNG (or any Pillow-readable raster), thresholds it to a chosen
bit depth, packs it into one of the formats understood by
`tiku_kits_gfx_image_t`, and emits a `.h`/`.c` pair declaring an
`extern const tiku_kits_gfx_image_t <name>`.

Usage:

    # Default: 1bpp row-major MSB-first (matches tiku_kits_gfx_bitmap)
    python3 tools/icon_bake.py --png logo.png --name logo

    # XBM-style (LSB-first)
    python3 tools/icon_bake.py --png logo.png --name logo --xbm

    # RLE-compressed (best for icons with large solid regions)
    python3 tools/icon_bake.py --png logo.png --name logo --rle

    # 4bpp grayscale (preserves shading; thresholded at 8 on a
    # 1-bit display)
    python3 tools/icon_bake.py --png logo.png --name logo --4bpp

    # 2bpp BWR (for 3-colour e-paper). Source PNG should use only
    # white, black, and red pixels; non-matching colours snap to
    # the nearest of those three.
    python3 tools/icon_bake.py --png logo.png --name logo --bwr

    # Invert and tweak threshold
    python3 tools/icon_bake.py --png logo.png --name logo \\
        --threshold 96 --invert

After running, `<name>.h` and `<name>.c` appear in the output dir
(default `tikukits/gfx/icons/`). Include the header from your app
and pass `&<name>` to any `tiku_kits_gfx_image_*` call.

Requires:  Pillow  (`pip install Pillow`)

SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow not installed; run `pip install Pillow`\n")
    sys.exit(2)


FORMATS = {
    "1bpp_msb":  "TIKU_KITS_GFX_IMG_1BPP_ROW_MSB",
    "1bpp_lsb":  "TIKU_KITS_GFX_IMG_1BPP_ROW_LSB",
    "1bpp_rle":  "TIKU_KITS_GFX_IMG_1BPP_RLE",
    "2bpp_bwr":  "TIKU_KITS_GFX_IMG_2BPP_BWR",
    "4bpp_gray": "TIKU_KITS_GFX_IMG_4BPP_GRAY",
}


# --------------------------------------------------------------------------
# Per-format encoders
# --------------------------------------------------------------------------

def encode_1bpp(pixels, w, h, lsb_first):
    """Pack a list of 0/1 ints into row-major bytes."""
    out = bytearray()
    bytes_per_row = (w + 7) // 8
    for y in range(h):
        for bx in range(bytes_per_row):
            b = 0
            for k in range(8):
                x = bx * 8 + k
                if x >= w:
                    break
                bit = pixels[y * w + x] & 1
                if lsb_first:
                    b |= bit << k
                else:
                    b |= bit << (7 - k)
            out.append(b)
    return bytes(out)


def encode_1bpp_rle(pixels, w, h):
    """Encode as bytes of (color << 7) | (run-1), runs 1..128."""
    out = bytearray()
    n = w * h
    i = 0
    while i < n:
        c = pixels[i] & 1
        run = 1
        while i + run < n and (pixels[i + run] & 1) == c and run < 128:
            run += 1
        out.append((c << 7) | (run - 1))
        i += run
    return bytes(out)


def encode_2bpp_bwr(pixels, w, h):
    """
    Each pixel is already mapped to {0=W, 1=B, 2=R, 3=transparent}.
    Pack 4 pixels per byte, high two bits = leftmost.
    """
    out = bytearray()
    pixels_per_row = w
    bytes_per_row = (pixels_per_row * 2 + 7) // 8
    for y in range(h):
        for bx in range(bytes_per_row):
            b = 0
            for k in range(4):
                x = bx * 4 + k
                if x >= w:
                    break
                v = pixels[y * w + x] & 3
                b |= v << (6 - k * 2)
            out.append(b)
    return bytes(out)


def encode_4bpp_gray(pixels, w, h):
    """Pack 2 pixels per byte, high nibble = leftmost."""
    out = bytearray()
    bytes_per_row = (w + 1) // 2
    for y in range(h):
        for bx in range(bytes_per_row):
            b = 0
            x0 = bx * 2
            x1 = x0 + 1
            v0 = pixels[y * w + x0] & 0x0F if x0 < w else 0
            v1 = pixels[y * w + x1] & 0x0F if x1 < w else 0
            b = (v0 << 4) | v1
            out.append(b)
    return bytes(out)


# --------------------------------------------------------------------------
# Pixel preparation per format
# --------------------------------------------------------------------------

def prepare_1bpp(img, threshold, invert):
    g = img.convert("L")
    pixels = []
    for y in range(g.height):
        for x in range(g.width):
            v = g.getpixel((x, y))
            bit = 1 if v < threshold else 0
            if invert:
                bit ^= 1
            pixels.append(bit)
    return pixels, g.width, g.height


def prepare_4bpp_gray(img, invert):
    g = img.convert("L")
    pixels = []
    for y in range(g.height):
        for x in range(g.width):
            v = g.getpixel((x, y))         # 0..255
            v = 15 - (v >> 4) if invert else (v >> 4)
            pixels.append(v & 0x0F)
        # Note: 0 is white, 15 is black after the shift only if invert is
        # used; the gfx kit's blit thresholds at 8 regardless.
    return pixels, g.width, g.height


def prepare_2bpp_bwr(img, invert):
    """Snap each pixel to one of {white, black, red}. Pixels with
    high alpha < 128 are emitted as transparent (3)."""
    rgba = img.convert("RGBA")
    pixels = []
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, gc, b, a = rgba.getpixel((x, y))
            if a < 128:
                pixels.append(3)         # transparent
                continue
            # Distance to each anchor.
            anchors = [
                (0, 255, 255, 255),     # 0 = white
                (1,   0,   0,   0),     # 1 = black
                (2, 255,   0,   0),     # 2 = red
            ]
            best = None
            best_d = None
            for code, ar, ag, ab in anchors:
                d = (r - ar) ** 2 + (gc - ag) ** 2 + (b - ab) ** 2
                if best_d is None or d < best_d:
                    best_d = d
                    best = code
            if invert:
                # Swap black <-> white; leave red.
                if best == 0:
                    best = 1
                elif best == 1:
                    best = 0
            pixels.append(best)
    return pixels, rgba.width, rgba.height


# --------------------------------------------------------------------------
# Emit C
# --------------------------------------------------------------------------

def emit(name, fmt_enum, w, h, data, out_dir, src_path):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    h_path = out / f"tiku_kits_gfx_image_{name}.h"
    c_path = out / f"tiku_kits_gfx_image_{name}.c"
    guard  = f"TIKU_KITS_GFX_IMAGE_{name.upper()}_H_"
    ts     = time.strftime("%Y-%m-%d %H:%M:%S")

    h_path.write_text(
        f"/*\n"
        f" * Auto-generated by tools/icon_bake.py at {ts}\n"
        f" * Source: {src_path}\n"
        f" *\n"
        f" * SPDX-License-Identifier: Apache-2.0\n"
        f" */\n\n"
        f"#ifndef {guard}\n"
        f"#define {guard}\n\n"
        f'#include "../tiku_kits_gfx_image.h"\n\n'
        f"extern const tiku_kits_gfx_image_t tiku_kits_gfx_image_{name};\n\n"
        f"#endif /* {guard} */\n"
    )

    cols_per_line = 12
    body = []
    for i in range(0, len(data), cols_per_line):
        chunk = data[i:i + cols_per_line]
        body.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    body_str = "\n".join(body)

    c_path.write_text(
        f"/*\n"
        f" * Auto-generated by tools/icon_bake.py at {ts}\n"
        f" * Source: {src_path}, {w}x{h} px, {len(data)} bytes\n"
        f" *\n"
        f" * SPDX-License-Identifier: Apache-2.0\n"
        f" */\n\n"
        f'#include "tiku_kits_gfx_image_{name}.h"\n\n'
        f"static const uint8_t image_{name}_data[] = {{\n"
        f"{body_str}\n"
        f"}};\n\n"
        f"const tiku_kits_gfx_image_t tiku_kits_gfx_image_{name} = {{\n"
        f"    .width    = {w},\n"
        f"    .height   = {h},\n"
        f"    .format   = {fmt_enum},\n"
        f"    .data     = image_{name}_data,\n"
        f"    .data_len = sizeof(image_{name}_data),\n"
        f"}};\n"
    )

    return h_path, c_path


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description="Bake a PNG icon into a TikuOS gfx_image_t C asset.")
    p.add_argument("--png", required=True, help="Input image (any Pillow format)")
    p.add_argument("--name", required=True,
                   help='Asset name (used as identifier suffix)')
    p.add_argument("--out", default="tikukits/gfx/icons",
                   help="Output directory (default: tikukits/gfx/icons)")

    fmt = p.add_mutually_exclusive_group()
    fmt.add_argument("--xbm",  action="store_true",
                     help="Use 1bpp LSB-first (XBM) packing")
    fmt.add_argument("--rle",  action="store_true",
                     help="Use 1bpp RLE compression")
    fmt.add_argument("--bwr",  action="store_true",
                     help="Use 2bpp BWR (white/black/red) packing")
    fmt.add_argument("--4bpp", dest="four_bpp", action="store_true",
                     help="Use 4bpp grayscale packing")

    p.add_argument("--threshold", type=int, default=128,
                   help="1bpp threshold (0..255, default 128)")
    p.add_argument("--invert", action="store_true",
                   help="Invert pixel polarity")

    args = p.parse_args()

    src = Path(args.png)
    if not src.exists():
        sys.stderr.write(f"error: {src} not found\n")
        return 1

    img = Image.open(src)

    if args.bwr:
        pixels, w, h = prepare_2bpp_bwr(img, args.invert)
        data = encode_2bpp_bwr(pixels, w, h)
        fmt_key = "2bpp_bwr"
    elif args.four_bpp:
        pixels, w, h = prepare_4bpp_gray(img, args.invert)
        data = encode_4bpp_gray(pixels, w, h)
        fmt_key = "4bpp_gray"
    elif args.rle:
        pixels, w, h = prepare_1bpp(img, args.threshold, args.invert)
        data = encode_1bpp_rle(pixels, w, h)
        fmt_key = "1bpp_rle"
    elif args.xbm:
        pixels, w, h = prepare_1bpp(img, args.threshold, args.invert)
        data = encode_1bpp(pixels, w, h, lsb_first=True)
        fmt_key = "1bpp_lsb"
    else:
        pixels, w, h = prepare_1bpp(img, args.threshold, args.invert)
        data = encode_1bpp(pixels, w, h, lsb_first=False)
        fmt_key = "1bpp_msb"

    fmt_enum = FORMATS[fmt_key]
    h_path, c_path = emit(args.name, fmt_enum, w, h, data, args.out, src)

    print(f"baked {src.name} -> {args.name}  "
          f"({w}x{h}, {fmt_key}, {len(data)} B)")
    print(f"  {h_path}")
    print(f"  {c_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
