#!/usr/bin/env python3
"""
Hold our rasteriser against freetype's, one glyph at a time.

A DEVELOPMENT tool.  The runtime ships no font library and the test suite
depends on none -- but a machine with PIL has freetype behind it, and that
is the only honest way to answer "is our own rasteriser any good?".

  tools/compare_freetype.py <font> [--sizes 10,12,14,16] [--text ...]
  tools/compare_freetype.py --sweep            # every system font it can

What it reports, per glyph:
  adv   the advance we compute against freetype's (must match; the advance
        comes from hmtx and has nothing to do with rasterising)
  ink   total coverage, ours over theirs (1.00 is identical weight;
        below 1 means our glyphs are lighter, which is what unhinted
        linear coverage looks like next to freetype's)
  mae   mean absolute coverage difference over the union box, 0-255

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""

import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, "glyphdump")

DEFAULT_TEXT = "AHOaeoxngpBRSMW0123.,"


def ours(font, px, cps):
    """Run glyphdump and return {cp: (adv, w, h, ox, oy, [rows])}."""
    args = [DUMP, font, str(px)] + ["%x" % c for c in cps]
    try:
        out = subprocess.run(args, capture_output=True, text=True,
                             timeout=60)
    except Exception as exc:                        # noqa: BLE001
        return None, "glyphdump failed: %s" % exc
    if out.returncode != 0 or out.stdout.startswith("REFUSED"):
        return None, "refused"
    glyphs, family, lines = {}, "", out.stdout.splitlines()
    i = 0
    while i < len(lines):
        parts = lines[i].split()
        if parts and parts[0] == "FAMILY":
            family = " ".join(parts[1:])
            i += 1
        elif parts and parts[0] == "GLYPH" and len(parts) >= 7:
            cp = int(parts[1], 16)
            adv, w, h, ox, oy = (int(v) for v in parts[2:7])
            rows = []
            for y in range(h):
                i += 1
                rows.append([int(v) for v in lines[i].split()])
            glyphs[cp] = (adv, w, h, ox, oy, rows)
            i += 1
        else:
            i += 1
    return (family, glyphs), None


def theirs(font, px, cps):
    """The same glyphs through PIL, which is freetype underneath."""
    from PIL import Image, ImageDraw, ImageFont

    face = ImageFont.truetype(font, px)
    out = {}
    for cp in cps:
        ch = chr(cp)
        box = face.getbbox(ch)
        if box is None or box[2] <= box[0] or box[3] <= box[1]:
            continue
        asc, _ = face.getmetrics()
        w, h = box[2] - box[0], box[3] - box[1]
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-box[0], -box[1]), ch, font=face, fill=255)
        rows = [[img.getpixel((x, y)) for x in range(w)] for y in range(h)]
        out[cp] = (int(round(face.getlength(ch))), w, h,
                   box[0], box[1] - asc, rows)
    return out


def at(g, x, y):
    """Coverage of glyph g at pen-relative (x, y), 0 outside it."""
    adv, w, h, ox, oy, rows = g
    gx, gy = x - ox, y - oy
    if 0 <= gx < w and 0 <= gy < h:
        return rows[gy][gx]
    return 0


def compare(a, b):
    """(advance delta, ink ratio, mean absolute difference)."""
    x0 = min(a[3], b[3])
    x1 = max(a[3] + a[1], b[3] + b[1])
    y0 = min(a[4], b[4])
    y1 = max(a[4] + a[2], b[4] + b[2])
    diff = n = ink_a = ink_b = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            va, vb = at(a, x, y), at(b, x, y)
            diff += abs(va - vb)
            ink_a += va
            ink_b += vb
            n += 1
    return (a[0] - b[0],
            (ink_a / ink_b) if ink_b else 0.0,
            (diff / n) if n else 0.0)


def run(font, sizes, text, quiet=False):
    cps = sorted({ord(c) for c in text})
    rows = []
    for px in sizes:
        mine, err = ours(font, px, cps)
        if mine is None:
            return None, err
        family, mine = mine
        ref = theirs(font, px, cps)
        shared = sorted(set(mine) & set(ref))
        if not shared:
            continue
        advs = inks = maes = 0.0
        bad_adv = 0
        for cp in shared:
            d, ink, mae = compare(mine[cp], ref[cp])
            advs += abs(d)
            bad_adv += 1 if d else 0
            inks += ink
            maes += mae
        n = len(shared)
        rows.append((px, n, bad_adv, inks / n, maes / n))
        if not quiet:
            print("  %2dpx  %3d glyphs   advance wrong: %-3d  "
                  "ink ours/theirs: %.2f   mean |diff|: %5.1f"
                  % (px, n, bad_adv, inks / n, maes / n))
    return rows, family


def main():
    args = sys.argv[1:]
    sizes = [10, 12, 14, 16]
    text = DEFAULT_TEXT
    if "--sizes" in args:
        i = args.index("--sizes")
        sizes = [int(v) for v in args[i + 1].split(",")]
        del args[i:i + 2]
    if "--text" in args:
        i = args.index("--text")
        text = args[i + 1]
        del args[i:i + 2]

    if args and args[0] == "--sweep":
        import glob

        every = sorted(glob.glob("/usr/share/fonts/**/*.ttf", recursive=True) +
                       glob.glob("/usr/share/fonts/**/*.otf", recursive=True))
        worst, refused, done = [], 0, 0
        for font in every:
            rows, family = run(font, [12], text, quiet=True)
            if rows is None:
                refused += 1
                continue
            if not rows:
                continue
            done += 1
            px, n, bad, ink, mae = rows[0]
            worst.append((mae, bad, font, family, n, ink))
        worst.sort(reverse=True)
        print("compared %d fonts at 12px; %d refused by us\n" % (done, refused))
        print("the ten our rasteriser agrees with LEAST:")
        for mae, bad, font, family, n, ink in worst[:10]:
            print("  %5.1f mae  %2d bad advances  %-28s %s"
                  % (mae, bad, (family or "?")[:28], os.path.basename(font)))
        if worst:
            avg = sum(w[0] for w in worst) / len(worst)
            allbad = sum(w[1] for w in worst)
            print("\n  mean |diff| across all fonts: %.1f of 255" % avg)
            print("  glyphs whose ADVANCE disagrees: %d" % allbad)
        return 0

    if not args:
        print(__doc__)
        return 2
    for font in args:
        print("%s" % font)
        rows, family = run(font, sizes, text)
        if rows is None:
            print("  (%s)" % family)
        else:
            print("  family: %s" % family)
    return 0


if __name__ == "__main__":
    sys.exit(main())
