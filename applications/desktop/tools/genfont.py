#!/usr/bin/env python3
"""
Generate the embedded UI font from a system TTF.

BeOS drew its interface in Swiss721 at 12 pt, antialiased.  DejaVu Sans at
12 px is the closest metric match available here; the glyphs are baked to
8-bit coverage so the runtime needs no font library and the wasm build
carries the same bytes.

Run: tools/genfont.py > src/tiku_desk_font_data.h

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""

import sys
from PIL import Image, ImageDraw, ImageFont

CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
]
BOLD = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
]
# The terminal draws on a grid, so it needs a face whose advance is the
# same for every glyph.  Nothing else in the interface uses it.
MONO = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
]
MONO_BOLD = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Bold.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
]
# The other interface families the picker offers.  Both were chosen for
# their metrics: at 10/12/14/16 px each sits within a pixel of DejaVu Sans,
# so the layout computed against the reference face survives the switch.
SERIF = [
    "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
]
SERIF_BOLD = [
    "/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
]
UIMONO = [
    "/usr/share/fonts/source-foundry-hack-fonts/Hack-Regular.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
]
UIMONO_BOLD = [
    "/usr/share/fonts/source-foundry-hack-fonts/Hack-Bold.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
]

MONO_SIZES = (13, 15)
SIZE = 12
FIRST, LAST = 32, 126


# name, regular, bold, and whether to bake the 2x faces.  Only the
# default family carries them: they are four times the coverage of the 1x
# face, and a family a person chose is drawn by replication on a scaled
# surface, which the renderer already falls back to.
FAMILIES = [
    ("Sans",  CANDIDATES, BOLD,        True),
    ("Serif", SERIF,      SERIF_BOLD,  False),
    ("Mono",  UIMONO,     UIMONO_BOLD, False),
]


def face_name(family, kind, tag):
    """The C identifier for one face.  Family 0 keeps the original names."""
    if family == 0:
        return "%s%s" % (kind, tag)
    return "f%d_%s%s" % (family, kind, tag)


def pick(paths):
    import os
    for p in paths:
        if os.path.exists(p):
            return p
    raise SystemExit("genfont: no candidate font found")


def render(path, name, out, size=SIZE, forced_adv=None, hi=None):
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    height = asc + desc
    glyphs = []
    blob = []

    for cp in range(FIRST, LAST + 1):
        ch = chr(cp)
        if forced_adv is not None:
            # The 2x face advances exactly twice the 1x face, so text at
            # scale 2 fills precisely the layout the 1x metrics promised.
            adv = forced_adv[cp - FIRST] * 2
        else:
            adv = int(round(font.getlength(ch)))
        box = font.getbbox(ch)
        w = max(1, box[2] - box[0])
        h = max(1, box[3] - box[1])
        img = Image.new("L", (w + 2, h + 2), 0)
        d = ImageDraw.Draw(img)
        d.text((-box[0] + 1, -box[1] + 1), ch, font=font, fill=255)
        px = list(img.getdata())
        # PIL measures from the ascender line; the renderer draws from the
        # baseline, so rebase here rather than in every draw call.
        glyphs.append({
            "cp": cp, "adv": adv,
            "w": img.width, "h": img.height,
            "ox": box[0] - 1, "oy": box[1] - 1 - asc,
            "off": len(blob),
        })
        blob.extend(px)

    out.write("/* %s: %s at %d px, %d glyphs, %d bytes of coverage. */\n"
              % (name, path.rsplit("/", 1)[-1], size, len(glyphs), len(blob)))
    out.write("static const tiku_desk_glyph_t %s_glyphs[] = {\n" % name)
    for g in glyphs:
        out.write("    {%d,%d,%d,%d,%d,%d,%d},\n"
                  % (g["cp"], g["adv"], g["w"], g["h"], g["ox"], g["oy"],
                     g["off"]))
    out.write("};\n")
    out.write("static const unsigned char %s_bits[] = {\n" % name)
    for i in range(0, len(blob), 24):
        out.write("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",\n")
    out.write("};\n")
    out.write("static const tiku_desk_font_t %s_font = {\n"
              "    %s_glyphs, %s_bits, %d, %d, %d, %d, %s\n};\n\n"
              % (name, name, name, len(glyphs), height, asc, FIRST,
                 ("&%s_font" % hi) if hi else "NULL"))
    return [g["adv"] for g in glyphs]


def main():
    out = sys.stdout
    out.write("/*\n"
              " * Tiku Desktop -- graphical interface to TikuOS devices.\n"
              " *\n"
              " * Authors: Ambuj Varshney <ambuj@tiku-os.org>\n"
              " *\n"
              " * tiku_desk_font_data.h - baked UI font coverage maps.\n"
              " *\n"
              " * GENERATED by tools/genfont.py -- do not edit by hand.\n"
              " *\n"
              " * SPDX-License-Identifier: Apache-2.0\n"
              " */\n"
              "#ifndef TIKU_DESK_FONT_DATA_H_\n"
              "#define TIKU_DESK_FONT_DATA_H_\n\n")
    sizes = (10, 12, 14, 16)
    null = open("/dev/null", "w")
    for family, (_, regular, bold, twice) in enumerate(FAMILIES):
        for px in sizes:
            tag = "" if px == SIZE else str(px)
            plain_name = face_name(family, "plain", tag)
            bold_name = face_name(family, "bold", tag)
            hi_plain = hi_bold = None

            if twice:
                plain_adv = render(pick(regular), "probe", null, size=px)
                bold_adv = render(pick(bold), "probe", null, size=px)
                hi_plain, hi_bold = plain_name + "2x", bold_name + "2x"
                render(pick(regular), hi_plain, out, size=px * 2,
                       forced_adv=plain_adv)
                render(pick(bold), hi_bold, out, size=px * 2,
                       forced_adv=bold_adv)
            render(pick(regular), plain_name, out, size=px, hi=hi_plain)
            render(pick(bold), bold_name, out, size=px, hi=hi_bold)
    for px in MONO_SIZES:
        # One advance for every glyph, taken from a character that has
        # nothing narrow about it, so the grid is exact.
        probe = ImageFont.truetype(pick(MONO), px)
        cell = int(round(probe.getlength("M")))
        render(pick(MONO), "mono%d" % px, out, size=px,
               forced_adv=[cell // 2] * (LAST - FIRST + 1))
        render(pick(MONO_BOLD), "monobold%d" % px, out, size=px,
               forced_adv=[cell // 2] * (LAST - FIRST + 1))
    out.write("/* The fixed-advance faces, smallest first. */\n")
    out.write("static const int mono_sizes[] = { %s };\n"
              % ", ".join(str(px) for px in MONO_SIZES))
    out.write("static const tiku_desk_font_t *const mono_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&mono%d_font" % px for px in MONO_SIZES))
    out.write("static const tiku_desk_font_t *const monobold_faces[] = {\n"
              "    %s\n};\n"
              % ", ".join("&monobold%d_font" % px for px in MONO_SIZES))

    out.write("/* Every size, smallest first, for the runtime picker. */\n")
    out.write("static const int face_sizes[] = { %s };\n"
              % ", ".join(str(px) for px in sizes))
    out.write("/* The families, in the order the picker offers them. */\n")
    out.write("static const char *const family_names[] = { %s };\n"
              % ", ".join('"%s"' % f[0] for f in FAMILIES))
    for face in ("plain", "bold"):
        out.write("static const tiku_desk_font_t *const %s_faces[][%d] = {\n"
                  % (face, len(sizes)))
        for family in range(len(FAMILIES)):
            out.write("    { %s },\n" % ", ".join(
                "&%s_font" % face_name(family, face,
                                       "" if px == SIZE else str(px))
                for px in sizes))
        out.write("};\n")
    out.write("#endif /* TIKU_DESK_FONT_DATA_H_ */\n")


if __name__ == "__main__":
    main()
