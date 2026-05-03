# tools/

Host-side utilities for TikuOS development. Run from the repo root.

## `font_bake.py` -- TTF/OTF -> tikukits/gfx bitmap font

Bakes any desktop or web font into a `tiku_kits_gfx_font_t` C struct
that the gfx kit can render directly. No runtime parser, no
rasterizer on the device -- the host does the work once at build
time.

### Why this approach?

E-paper displays don't benefit from antialiasing (1 bit per pixel
on monochrome panels, 2 bits per pixel on BWR). Runtime TTF
rasterization (~5 KB code via stb_truetype + 50-200 KB FRAM per
font file + scratch SRAM) would buy us nothing -- the output is
the same pixel grid we'd get from baking offline. So bake offline
and pay only for the rasterized bitmap (~1-5 KB per typical font).

### Requirements

```bash
pip install Pillow
```

Pillow needs FreeType available to read TTF files; on Linux this
comes with `libfreetype6`, on macOS via the system, on Windows
via the wheel.

### Usage

```bash
# Default: monospace, ASCII range, write to tikukits/gfx/fonts/
python3 tools/font_bake.py --ttf /path/to/Hack-Regular.ttf \
                           --size 16 --name hack16

# Custom range (Latin-1 supplement)
python3 tools/font_bake.py --ttf JetBrainsMono.ttf \
                           --size 12 --range 0x20-0xff --name jbm12

# Proportional (variable-width) font
python3 tools/font_bake.py --ttf IBMPlexSans.ttf \
                           --size 10 --proportional --name plex10

# Force a specific monospace cell width (default = max glyph advance)
python3 tools/font_bake.py --ttf SourceCodePro.ttf \
                           --size 14 --width 8 --name scp14
```

After running:

1. New `.h` and `.c` files appear under `tikukits/gfx/fonts/`.
2. The Makefile globs `fonts/*.c`, so they're picked up
   automatically the next build.
3. In app code:

   ```c
   #include <tikukits/gfx/fonts/tiku_kits_gfx_font_hack16.h>

   tiku_kits_gfx_draw_string(surface, x, y, "Hello",
                              &tiku_kits_gfx_font_hack16,
                              TIKU_KITS_GFX_BLACK, 1);
   ```

### Options

| Flag            | Default                | Notes                                 |
|-----------------|------------------------|---------------------------------------|
| `--ttf PATH`    | (required)             | TTF/OTF/anything Pillow can open      |
| `--size N`      | (required)             | Font point size; pixel height derives from font metrics |
| `--name STR`    | (required)             | C identifier suffix                   |
| `--range A-B`   | `0x20-0x7E`            | Inclusive ASCII range, hex or decimal |
| `--out DIR`     | `tikukits/gfx/fonts`   | Output directory                      |
| `--threshold N` | `128`                  | 0..255 cutoff for 1-bit conversion. Lower = bolder, higher = thinner |
| `--proportional` | (off, monospace)      | Emit per-glyph widths table           |
| `--width N`     | (auto = max glyph adv) | Force monospace cell width            |

### Memory cost

| Glyph cell | Bytes/glyph | 95-char ASCII font |
|-----------|-------------|---------------------|
| 5x7       | 5           | 475 B               |
| 6x8       | 6           | 570 B               |
| 8x16      | 16          | 1520 B              |
| 12x24     | 36          | 3420 B              |
| 16x32     | 64          | 6080 B              |

Plus 95 bytes if `--proportional` is used (per-glyph widths table).
Built with `MEMORY_MODEL=large`, all of this lands in HIFRAM
automatically -- no SRAM cost.

### Output format

Identical to the built-in 5x7 font (`tiku_kits_gfx_font_5x7`):

- Column-major glyph data: each column packed top-to-bottom into
  one or more bytes (LSB = top row).
- For glyphs taller than 8 px, multiple bytes per column stacked.
- The struct sets `bytes_per_column = ceil(height / 8)`.
- Monospace: `widths = NULL`, glyph storage = `width * bytes_per_column`.
- Proportional: `widths` points to a per-glyph table; glyph storage
  is still `width * bytes_per_column` per glyph (some columns
  unused for narrow glyphs); only the rendered advance shrinks.

### Recommended fonts

Open-licensed, small, render well on EPD:

- **Hack** (https://github.com/source-foundry/Hack) -- monospace, very legible at small sizes
- **JetBrains Mono** (https://www.jetbrains.com/mono/) -- monospace, ligatures work fine
- **IBM Plex Sans / Mono** (https://github.com/IBM/plex) -- proportional + monospace
- **Source Code Pro** (https://github.com/adobe-fonts/source-code-pro) -- monospace
- **Inter** (https://rsms.me/inter/) -- proportional UI font
- **Atkinson Hyperlegible** (https://brailleinstitute.org/freefont) -- maximum-contrast
  proportional font designed for low-vision readers; very EPD-friendly
