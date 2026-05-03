#!/usr/bin/env python3
"""
font_bake.py -- Convert TTF/OTF fonts into TikuOS gfx-kit C arrays.

Takes any desktop font + a target pixel size + a character range,
rasterizes each glyph on the host using Pillow, thresholds to 1-bit,
packs into the column-major / LSB-top format that
`tiku_kits_gfx_font_t` expects, and emits a matching `.h` + `.c`
pair under `tikukits/gfx/fonts/` (or wherever you point it).

Usage:

    # Default: monospace, ASCII range, write to tikukits/gfx/fonts/
    python3 tools/font_bake.py --ttf /path/to/Hack.ttf \\
                                --size 16 --name hack16

    # Custom range and output dir
    python3 tools/font_bake.py --ttf JetBrainsMono.ttf \\
                                --size 12 --range 0x20-0xff \\
                                --name jbm12 --out /tmp/

    # Proportional (variable-width) font
    python3 tools/font_bake.py --ttf IBMPlexSans.ttf \\
                                --size 10 --proportional \\
                                --name plex10

After running:

    1. The new `.h` and `.c` files appear under `tikukits/gfx/fonts/`.
    2. The Makefile globs `fonts/*.c`, so they're picked up automatically.
    3. In your app, include the header and pass `&tiku_kits_gfx_font_<name>`
       to any text-drawing call.

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
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.stderr.write(
        "Missing Pillow. Install with: pip install Pillow\n"
    )
    sys.exit(2)


# ---------------------------------------------------------------------------
# Range parsing
# ---------------------------------------------------------------------------
def parse_range(spec: str) -> tuple[int, int]:
    """Accept '0x20-0x7e', '32-126', or a single-codepoint '0x41'."""
    spec = spec.strip()
    if "-" in spec:
        lo_s, hi_s = spec.split("-", 1)
    else:
        lo_s = hi_s = spec
    lo = int(lo_s, 0)  # auto-detect base via 0x prefix
    hi = int(hi_s, 0)
    if lo > hi:
        raise ValueError(f"range start ({lo}) > end ({hi})")
    if lo < 0 or hi > 0xFF:
        raise ValueError(f"range {spec} out of byte (0..255)")
    return lo, hi


# ---------------------------------------------------------------------------
# Glyph rendering
# ---------------------------------------------------------------------------
def measure_font_height(font: ImageFont.FreeTypeFont) -> tuple[int, int, int]:
    """Return (height, ascent, descent) using the font's own metrics."""
    ascent, descent = font.getmetrics()
    return ascent + descent, ascent, descent


def glyph_width(font: ImageFont.FreeTypeFont, ch: str) -> int:
    """Return the advance width Pillow recommends for @ch."""
    # Pillow >= 10 deprecates getsize; use getbbox / getlength instead.
    try:
        w = int(round(font.getlength(ch)))
        if w > 0:
            return w
    except AttributeError:
        pass
    try:
        bbox = font.getbbox(ch)
        if bbox is not None:
            return max(bbox[2] - bbox[0], 0)
    except AttributeError:
        pass
    return 0


def render_glyph(font: ImageFont.FreeTypeFont, ch: str,
                  height: int, max_w: int, threshold: int) -> list[int]:
    """Render @ch into a width x height bitmap and return the
    list-of-rows pixel values (1 = lit, 0 = clear). Width = max_w
    (callers use a fixed canvas; per-glyph advance is recorded
    separately for proportional output)."""
    img = Image.new("L", (max_w, height), 0)  # 8-bit greyscale, black bg
    draw = ImageDraw.Draw(img)
    # Pillow's text() positions the top of the ascender box at y=0,
    # so a canvas of height = ascent + descent fits the glyph.
    draw.text((0, 0), ch, font=font, fill=255)

    px = img.load()
    rows: list[list[int]] = []
    for y in range(height):
        row = [(1 if px[x, y] >= threshold else 0) for x in range(max_w)]
        rows.append(row)
    return rows


def pack_glyph_columns(rows: list[list[int]],
                        glyph_w: int,
                        bytes_per_column: int) -> list[int]:
    """Pack rows[y][x] into the gfx kit's column-major / LSB-top
    format: width * bytes_per_column bytes per glyph."""
    out: list[int] = []
    height = len(rows)
    for x in range(glyph_w):
        for byte_idx in range(bytes_per_column):
            byte = 0
            for bit in range(8):
                row = byte_idx * 8 + bit
                if row >= height:
                    break
                if rows[row][x]:
                    byte |= (1 << bit)
            out.append(byte)
    return out


# ---------------------------------------------------------------------------
# C emission
# ---------------------------------------------------------------------------
HEADER_TMPL = """\
/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * tiku_kits_gfx_font_{name}.h - Auto-generated bitmap font
 *
 * Source font  : {ttf_basename}
 * Size         : {size} pt -> {height} px ({ascent} ascent + {descent} descent)
 * Range        : 0x{first:02X} .. 0x{last:02X}  ({n_glyphs} glyphs)
 * Layout       : {layout}
 * Glyph data   : {n_bytes} bytes
 * Generated by : tools/font_bake.py at {timestamp}
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_KITS_GFX_FONT_{name_upper}_H_
#define TIKU_KITS_GFX_FONT_{name_upper}_H_

#include "../tiku_kits_gfx_text.h"

extern const tiku_kits_gfx_font_t tiku_kits_gfx_font_{name};

#endif /* TIKU_KITS_GFX_FONT_{name_upper}_H_ */
"""

SOURCE_TMPL = """\
/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * tiku_kits_gfx_font_{name}.c - Auto-generated bitmap font data
 *
 * Source font  : {ttf_basename}
 * Size         : {size} pt -> {height} px ({ascent} ascent + {descent} descent)
 * Range        : 0x{first:02X} .. 0x{last:02X}  ({n_glyphs} glyphs)
 * Layout       : {layout}
 * Generated by : tools/font_bake.py at {timestamp}
 *
 * Glyph format: column-major, 1 bit per pixel, byte 0 LSB = top row.
 * For glyphs taller than 8 px, multiple bytes per column are stacked
 * top-to-bottom. {bytes_per_column} byte(s) per column, {bytes_per_glyph}
 * bytes per glyph.
 *
 * Do not edit by hand -- regenerate from the source font.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_kits_gfx_font_{name}.h"

static const uint8_t font_{name}_glyphs[] = {{
{glyph_data}
}};
{widths_block}
const tiku_kits_gfx_font_t tiku_kits_gfx_font_{name} = {{
    .width             = {width},
    .height            = {height},
    .first             = 0x{first:02X},
    .last              = 0x{last:02X},
    .bytes_per_column  = {bytes_per_column},
    .glyphs            = font_{name}_glyphs,
    .widths            = {widths_field},
    .ascent            = {ascent},
    .descent           = {descent},
    .line_height       = {line_height},
}};
"""


def fmt_byte_array(data: list[int], indent: str = "    ",
                    bytes_per_line: int = 12,
                    glyph_size: int | None = None,
                    first_codepoint: int = 0x20) -> str:
    """Format a byte array nicely. If glyph_size is given, group
    bytes into per-glyph rows with a comment showing the codepoint."""
    out_lines: list[str] = []
    if glyph_size is None or glyph_size <= 0:
        for i in range(0, len(data), bytes_per_line):
            chunk = data[i:i + bytes_per_line]
            out_lines.append(indent + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        return "\n".join(out_lines)

    # Per-glyph rows.
    n_glyphs = len(data) // glyph_size
    for g in range(n_glyphs):
        glyph = data[g * glyph_size:(g + 1) * glyph_size]
        cp = first_codepoint + g
        ch = chr(cp) if 0x20 <= cp <= 0x7E else "?"
        out_lines.append(
            indent
            + f"/* 0x{cp:02X} '{ch}' */ "
            + ", ".join(f"0x{b:02x}" for b in glyph) + ","
        )
    return "\n".join(out_lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> int:
    p = argparse.ArgumentParser(
        description="Bake a TTF/OTF font into a TikuOS gfx-kit bitmap font.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--ttf", required=True,
                   help="path to TTF/OTF file (any Pillow-readable font)")
    p.add_argument("--size", type=int, required=True,
                   help="font point size (Pillow's --size; pixel height "
                        "is derived from font metrics)")
    p.add_argument("--name", required=True,
                   help="C identifier suffix (font becomes tiku_kits_gfx_font_<name>)")
    p.add_argument("--range", default="0x20-0x7E",
                   help="ASCII range (default: 0x20-0x7E, printable ASCII)")
    p.add_argument("--out", default="tikukits/gfx/fonts",
                   help="output directory (default: tikukits/gfx/fonts)")
    p.add_argument("--threshold", type=int, default=128,
                   help="greyscale threshold for 1-bit conversion "
                        "(0..255, default 128)")
    p.add_argument("--proportional", action="store_true",
                   help="emit a per-glyph widths table (default: monospace)")
    p.add_argument("--width", type=int, default=0,
                   help="force monospace cell width (0 = auto = max glyph "
                        "advance in the range)")
    args = p.parse_args()

    ttf_path = Path(args.ttf)
    if not ttf_path.is_file():
        print(f"error: TTF not found: {ttf_path}", file=sys.stderr)
        return 1

    # --- Load font -----------------------------------------------------
    font = ImageFont.truetype(str(ttf_path), size=args.size)
    height, ascent, descent = measure_font_height(font)
    bytes_per_column = (height + 7) // 8

    # --- Resolve character range --------------------------------------
    first, last = parse_range(args.range)
    n_glyphs = last - first + 1

    # --- Per-glyph advance widths -------------------------------------
    advances: list[int] = []
    for cp in range(first, last + 1):
        ch = chr(cp)
        w = glyph_width(font, ch)
        if w == 0:
            # Whitespace and missing glyphs: fall back to ~1/3 height.
            w = max(1, height // 3)
        advances.append(w)

    max_advance = max(advances)
    if args.width > 0:
        cell_width = args.width
        if cell_width < max_advance:
            print(f"warning: --width {cell_width} < max glyph "
                   f"advance {max_advance}; wider glyphs will be clipped.",
                   file=sys.stderr)
    else:
        cell_width = max_advance

    # --- Render every glyph -------------------------------------------
    glyph_bytes: list[int] = []
    for cp in range(first, last + 1):
        ch = chr(cp)
        rows = render_glyph(font, ch, height, cell_width, args.threshold)
        if args.proportional:
            gw = min(advances[cp - first], cell_width)
        else:
            gw = cell_width
        # Pad: storage is always cell_width columns (sparse for narrow
        # glyphs). Render the whole canvas; only the first gw columns
        # carry per-glyph pixels but we still allocate cell_width columns.
        packed = pack_glyph_columns(rows, cell_width, bytes_per_column)
        glyph_bytes.extend(packed)

    bytes_per_glyph = cell_width * bytes_per_column
    total_bytes = len(glyph_bytes)

    # --- Assemble C output --------------------------------------------
    layout = "monospace" if not args.proportional else "proportional (per-glyph widths)"
    name = args.name.lower().replace("-", "_").replace(".", "_")
    name_upper = name.upper()
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")

    if args.proportional:
        widths_block = (
            f"\nstatic const uint8_t font_{name}_widths[{n_glyphs}] = {{\n"
            + fmt_byte_array(advances, bytes_per_line=16)
            + "\n};\n"
        )
        widths_field = f"font_{name}_widths"
    else:
        widths_block = ""
        widths_field = "NULL"

    glyph_data = fmt_byte_array(glyph_bytes,
                                 glyph_size=bytes_per_glyph,
                                 first_codepoint=first)

    common = dict(
        name=name, name_upper=name_upper,
        ttf_basename=ttf_path.name,
        size=args.size,
        height=height, ascent=ascent, descent=descent,
        first=first, last=last,
        n_glyphs=n_glyphs,
        layout=layout,
        n_bytes=total_bytes,
        bytes_per_column=bytes_per_column,
        bytes_per_glyph=bytes_per_glyph,
        timestamp=timestamp,
        width=cell_width,
        line_height=height + 1,
        widths_block=widths_block,
        widths_field=widths_field,
        glyph_data=glyph_data,
    )

    header_text = HEADER_TMPL.format(**common)
    source_text = SOURCE_TMPL.format(**common)

    # --- Write files --------------------------------------------------
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    h_path = out_dir / f"tiku_kits_gfx_font_{name}.h"
    c_path = out_dir / f"tiku_kits_gfx_font_{name}.c"
    h_path.write_text(header_text)
    c_path.write_text(source_text)

    # --- Report -------------------------------------------------------
    print(f"  source font: {ttf_path.name} ({args.size} pt)")
    print(f"  pixel size : {cell_width} x {height}  (ascent {ascent}, descent {descent})")
    print(f"  range      : 0x{first:02X} .. 0x{last:02X}  ({n_glyphs} glyphs)")
    print(f"  layout     : {layout}")
    print(f"  glyph data : {total_bytes} bytes "
          f"({bytes_per_glyph} bytes per glyph)")
    if args.proportional:
        print(f"  + widths   : {n_glyphs} bytes")
    print(f"  wrote: {h_path}")
    print(f"  wrote: {c_path}")
    print()
    print(f"  use in app:")
    print(f"    #include <tikukits/gfx/fonts/tiku_kits_gfx_font_{name}.h>")
    print(f"    tiku_kits_gfx_draw_string(s, x, y, \"hi\",")
    print(f"        &tiku_kits_gfx_font_{name}, color, scale);")
    return 0


if __name__ == "__main__":
    sys.exit(main())
