#!/usr/bin/env python3
"""How much of a glyph is neither ink nor paper?

A DEVELOPMENT tool.  Mean absolute difference against freetype cannot
settle whether grid-fitting helps: if freetype snaps an edge up and we
snap it down, that reads as twice the error of not snapping at all, so
the measure quietly prefers a blur.  This measures the thing hinting is
FOR instead -- the share of a glyph's pixels sitting at neither end of
the scale, which is what a soft horizontal edge looks like from close up.

Lower is crisper.  freetype's own number is the target, not zero.

  tools/crispness.py [sizes]
"""
import glob, os, sys, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cf", os.path.join(HERE, "compare_freetype.py"))
cf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cf)

sizes = [int(v) for v in (sys.argv[1].split(",") if len(sys.argv) > 1 else ["10", "12"])]
cps = sorted({ord(c) for c in cf.DEFAULT_TEXT})

def gray_share(glyphs):
    """Share of INKED pixels that are ambiguous rather than committed."""
    gray = inked = 0
    for cp, g in glyphs.items():
        for row in g[5]:
            for v in row:
                if v > 8:
                    inked += 1
                    if 48 <= v <= 208:
                        gray += 1
    return (gray / inked) if inked else None

fonts = sorted(glob.glob("/usr/share/fonts/**/*.ttf", recursive=True) +
               glob.glob("/usr/share/fonts/**/*.otf", recursive=True))
on_t = off_t = ref_t = 0.0
n = 0
crisper = softer = 0
for font in fonts:
    for px in sizes:
        os.environ.pop("TIKU_DESK_HINT", None)
        a, _ = cf.ours(font, px, cps)
        os.environ["TIKU_DESK_HINT"] = "0"
        b, _ = cf.ours(font, px, cps)
        os.environ.pop("TIKU_DESK_HINT", None)
        if a is None or b is None:
            continue
        try:
            ref = cf.theirs(font, px, cps)
        except Exception:
            continue
        ga, gb, gr = gray_share(a[1]), gray_share(b[1]), gray_share(ref)
        if None in (ga, gb, gr):
            continue
        on_t += ga; off_t += gb; ref_t += gr; n += 1
        if ga < gb - 0.005: crisper += 1
        elif ga > gb + 0.005: softer += 1
print("font/size pairs: %d   sizes: %s" % (n, sizes))
if n:
    print("  ambiguous share of ink -- lower is crisper")
    print("    ours, fit OFF : %.3f" % (off_t / n))
    print("    ours, fit ON  : %.3f" % (on_t / n))
    print("    freetype      : %.3f   <- the target" % (ref_t / n))
    print("  the fit made %d crisper, %d softer" % (crisper, softer))
