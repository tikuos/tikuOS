/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_ttf.c - reading a font somebody dropped in (see the header).
 *
 * Every read is bounds-checked against the mapped length.  These files
 * arrive by being dropped in a folder, which is to say from anywhere, and
 * a font is a format with offsets pointing at offsets: the parser must
 * treat a truncated or hostile file as simply a file it cannot draw with.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_ttf.h"

#include "tiku_desk_cff.h"
#include "tiku_desk_glyphpath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TTF_MAX_POINTS   4096
#define TTF_MAX_DEPTH    5      /* composite glyphs referring to composites */
#define TTF_MAX_SIZE     512    /* a glyph bigger than this is not drawn */

struct tiku_desk_ttf {
    unsigned char *data;
    size_t         len;
    size_t         glyf, loca, hmtx, cmap_sub;
    size_t         glyf_len, loca_len, hmtx_len;
    int            upem;
    int            long_loca;
    int            cmap_format;
    int            num_glyphs;
    int            num_hmetrics;
    int            ascent, descent;
    int            bold;
    char           family[64];
    /* An OpenType file with PostScript outlines keeps them here instead
     * of in glyf, and everything else about it is the same. */
    tiku_desk_cff_t *cff;
    float            cff_upem;
    /* Where the lowercase and the capitals reach, in font units: the
     * two lines a reader's eye follows, and so the two worth snapping
     * to whole pixels.  Measured once, on the first glyph asked for. */
    size_t           os2;
    float            zone_x, zone_cap;
    int              zones;
};

/* ---------------------------------------------------------------- bytes */

static unsigned
rd16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static int
rds16(const unsigned char *p)
{
    return (int)(short)rd16(p);
}

static unsigned long
rd32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

/** @brief Whether @p need bytes at @p off are inside the file. */
static int
fits(const tiku_desk_ttf_t *t, size_t off, size_t need)
{
    return off <= t->len && need <= t->len - off;
}

/* ---------------------------------------------------------------- tables */

/** @brief Find a table by tag.  @return its offset, or 0 when absent. */
static size_t
table_of(const tiku_desk_ttf_t *t, size_t dir, const char *tag, size_t *len)
{
    unsigned n, i;

    if (!fits(t, dir, 12u)) {
        return 0;
    }
    n = rd16(t->data + dir + 4);
    for (i = 0; i < n; i++) {
        size_t rec = dir + 12u + (size_t)i * 16u;
        size_t off, length;

        if (!fits(t, rec, 16u)) {
            return 0;
        }
        if (memcmp(t->data + rec, tag, 4) != 0) {
            continue;
        }
        off = (size_t)rd32(t->data + rec + 8);
        length = (size_t)rd32(t->data + rec + 12);
        if (!fits(t, off, length)) {
            return 0;
        }
        if (len != NULL) {
            *len = length;
        }
        return off;
    }
    return 0;
}

/** @brief The family name, out of the name table's many encodings. */
static void
read_family(tiku_desk_ttf_t *t, size_t name)
{
    unsigned count, storage, i;
    int best = -1;
    size_t best_off = 0;
    unsigned best_len = 0, best_wide = 0;

    if (name == 0 || !fits(t, name, 6u)) {
        return;
    }
    count = rd16(t->data + name + 2);
    storage = rd16(t->data + name + 4);
    for (i = 0; i < count; i++) {
        size_t rec = name + 6u + (size_t)i * 12u;
        unsigned platform, encoding, id, length, off;
        int rank;

        if (!fits(t, rec, 12u)) {
            break;
        }
        platform = rd16(t->data + rec);
        encoding = rd16(t->data + rec + 2);
        id = rd16(t->data + rec + 6);
        length = rd16(t->data + rec + 8);
        off = rd16(t->data + rec + 10);
        /* 16 is the typographic family ("Helvetica Neue"); 1 is the one
         * split into four so that old menus could list styles. */
        if (id != 1u && id != 16u) {
            continue;
        }
        rank = (id == 16u) ? 2 : 1;
        if (rank <= best) {
            continue;
        }
        if (!fits(t, name + storage + off, length)) {
            continue;
        }
        best = rank;
        best_off = name + storage + off;
        best_len = length;
        best_wide = (platform == 3u || (platform == 0u)) ||
                    (platform == 3u && encoding == 1u);
    }
    if (best < 0) {
        return;
    }
    {
        /* UTF-16BE for the Windows and Unicode platforms, one byte per
         * letter for Mac Roman.  Either way we keep what is ASCII: it is
         * a name to pick from a list, not a document. */
        unsigned i2, out = 0;
        unsigned step = best_wide ? 2u : 1u;

        for (i2 = 0; i2 + step <= best_len &&
                     out + 1 < sizeof t->family; i2 += step) {
            unsigned c = best_wide ? rd16(t->data + best_off + i2)
                                   : t->data[best_off + i2];

            if (c >= 32u && c < 127u) {
                t->family[out++] = (char)c;
            }
        }
        t->family[out] = '\0';
    }
}

/** @brief Pick the character map we can read, preferring the widest. */
static void
read_cmap(tiku_desk_ttf_t *t, size_t cmap)
{
    unsigned n, i;
    int best = -1;

    if (cmap == 0 || !fits(t, cmap, 4u)) {
        return;
    }
    n = rd16(t->data + cmap + 2);
    for (i = 0; i < n; i++) {
        size_t rec = cmap + 4u + (size_t)i * 8u;
        size_t sub;
        unsigned platform, encoding, format;
        int rank;

        if (!fits(t, rec, 8u)) {
            break;
        }
        platform = rd16(t->data + rec);
        encoding = rd16(t->data + rec + 2);
        sub = cmap + (size_t)rd32(t->data + rec + 4);
        if (!fits(t, sub, 4u)) {
            continue;
        }
        format = rd16(t->data + sub);
        if (format == 12u) {
            rank = 3;
        } else if (format == 4u) {
            rank = 2;
        } else if (format == 6u || format == 0u) {
            rank = 1;
        } else {
            continue;
        }
        /* A symbol map (3,0) claims the private use area and would hand
         * back a padlock for the letter A. */
        if (platform == 3u && encoding == 0u) {
            rank = 0;
        }
        if (rank <= best) {
            continue;
        }
        best = rank;
        t->cmap_sub = sub;
        t->cmap_format = (int)format;
    }
}

/** @brief The glyph @p cp maps to, or 0 (which is .notdef). */
static unsigned
glyph_index(const tiku_desk_ttf_t *t, unsigned cp)
{
    size_t sub = t->cmap_sub;

    if (sub == 0) {
        return 0;
    }
    if (t->cmap_format == 4) {
        unsigned segs, i;
        size_t ends, starts, deltas, ranges;

        if (!fits(t, sub, 14u)) {
            return 0;
        }
        if (cp > 0xFFFFu) {
            return 0;
        }
        segs = rd16(t->data + sub + 6) / 2u;
        ends = sub + 14u;
        starts = ends + (size_t)segs * 2u + 2u;
        deltas = starts + (size_t)segs * 2u;
        ranges = deltas + (size_t)segs * 2u;
        if (!fits(t, ranges, (size_t)segs * 2u)) {
            return 0;
        }
        for (i = 0; i < segs; i++) {
            unsigned end = rd16(t->data + ends + i * 2u);
            unsigned start, delta, range;

            if (cp > end) {
                continue;
            }
            start = rd16(t->data + starts + i * 2u);
            if (cp < start) {
                return 0;
            }
            delta = rd16(t->data + deltas + i * 2u);
            range = rd16(t->data + ranges + i * 2u);
            if (range == 0u) {
                return (cp + delta) & 0xFFFFu;
            }
            {
                size_t at = ranges + i * 2u + range + (cp - start) * 2u;
                unsigned g;

                if (!fits(t, at, 2u)) {
                    return 0;
                }
                g = rd16(t->data + at);
                return (g == 0u) ? 0u : ((g + delta) & 0xFFFFu);
            }
        }
        return 0;
    }
    if (t->cmap_format == 12) {
        unsigned long groups, i;

        if (!fits(t, sub, 16u)) {
            return 0;
        }
        groups = rd32(t->data + sub + 12);
        if (groups > 100000ul ||
            !fits(t, sub + 16u, (size_t)groups * 12u)) {
            return 0;
        }
        for (i = 0; i < groups; i++) {
            size_t g = sub + 16u + (size_t)i * 12u;
            unsigned long first = rd32(t->data + g);
            unsigned long last = rd32(t->data + g + 4);

            if (cp >= first && cp <= last) {
                return (unsigned)(rd32(t->data + g + 8) + (cp - first));
            }
        }
        return 0;
    }
    if (t->cmap_format == 6) {
        unsigned first, count;

        if (!fits(t, sub, 10u)) {
            return 0;
        }
        first = rd16(t->data + sub + 6);
        count = rd16(t->data + sub + 8);
        if (cp < first || cp >= first + count ||
            !fits(t, sub + 10u + (cp - first) * 2u, 2u)) {
            return 0;
        }
        return rd16(t->data + sub + 10u + (cp - first) * 2u);
    }
    if (t->cmap_format == 0) {
        if (cp > 255u || !fits(t, sub + 6u + cp, 1u)) {
            return 0;
        }
        return t->data[sub + 6u + cp];
    }
    return 0;
}

/** @brief The advance width of glyph @p g, in font units. */
static int
advance_of(const tiku_desk_ttf_t *t, unsigned g)
{
    unsigned at = (g < (unsigned)t->num_hmetrics)
                      ? g : (unsigned)(t->num_hmetrics - 1);

    if (t->num_hmetrics <= 0 || !fits(t, t->hmtx + (size_t)at * 4u, 2u)) {
        return 0;
    }
    return (int)rd16(t->data + t->hmtx + (size_t)at * 4u);
}

/** @brief Where glyph @p g's outline lives.  @return its length. */
static size_t
glyph_at(const tiku_desk_ttf_t *t, unsigned g, size_t *off)
{
    size_t a, b;

    if ((int)g >= t->num_glyphs) {
        return 0;
    }
    if (t->long_loca) {
        if (!fits(t, t->loca + (size_t)g * 4u, 8u)) {
            return 0;
        }
        a = (size_t)rd32(t->data + t->loca + (size_t)g * 4u);
        b = (size_t)rd32(t->data + t->loca + (size_t)g * 4u + 4u);
    } else {
        if (!fits(t, t->loca + (size_t)g * 2u, 4u)) {
            return 0;
        }
        a = (size_t)rd16(t->data + t->loca + (size_t)g * 2u) * 2u;
        b = (size_t)rd16(t->data + t->loca + (size_t)g * 2u + 2u) * 2u;
    }
    if (b <= a || b > t->glyf_len) {
        return 0;               /* an empty glyph: a space, legitimately */
    }
    *off = t->glyf + a;
    return b - a;
}

/* -------------------------------------------------------------- outlines */

typedef struct {
    float x, y;
    int   on;
} ttf_point_t;

typedef struct {
    ttf_point_t pt[TTF_MAX_POINTS];
    int         end[64];
    int         n;
    int         contours;
} ttf_outline_t;

static int read_outline(const tiku_desk_ttf_t *t, unsigned g,
                        ttf_outline_t *out, float dx, float dy, int depth);

/** @brief A simple glyph: flags, then x deltas, then y deltas. */
static int
read_simple(const tiku_desk_ttf_t *t, size_t off, size_t len, int contours,
            ttf_outline_t *out, float dx, float dy)
{
    size_t p = off + 10u;
    int points, i;
    unsigned char flags[TTF_MAX_POINTS];
    int base = out->n;
    unsigned ins;

    if ((size_t)contours * 2u + 12u > len || contours > 64) {
        return 0;
    }
    points = (int)rd16(t->data + p + (size_t)(contours - 1) * 2u) + 1;
    if (points <= 0 || points > TTF_MAX_POINTS ||
        base + points > TTF_MAX_POINTS ||
        out->contours + contours > (int)(sizeof out->end / sizeof out->end[0])) {
        return 0;
    }
    for (i = 0; i < contours; i++) {
        out->end[out->contours + i] =
            base + (int)rd16(t->data + p + (size_t)i * 2u);
    }
    p += (size_t)contours * 2u;
    if (!fits(t, p, 2u)) {
        return 0;
    }
    ins = rd16(t->data + p);
    p += 2u + ins;              /* the hinting programme, which we skip */

    for (i = 0; i < points; ) {
        unsigned char f;

        if (!fits(t, p, 1u)) {
            return 0;
        }
        f = t->data[p++];
        flags[i++] = f;
        if ((f & 8u) != 0u) {   /* repeat */
            unsigned r;

            if (!fits(t, p, 1u)) {
                return 0;
            }
            r = t->data[p++];
            while (r-- > 0u && i < points) {
                flags[i++] = f;
            }
        }
    }
    {
        int v = 0;

        for (i = 0; i < points; i++) {
            unsigned char f = flags[i];

            if ((f & 2u) != 0u) {
                if (!fits(t, p, 1u)) {
                    return 0;
                }
                v += ((f & 16u) != 0u) ? (int)t->data[p] : -(int)t->data[p];
                p += 1u;
            } else if ((f & 16u) == 0u) {
                if (!fits(t, p, 2u)) {
                    return 0;
                }
                v += rds16(t->data + p);
                p += 2u;
            }
            out->pt[base + i].x = (float)v + dx;
            out->pt[base + i].on = ((f & 1u) != 0u);
        }
        v = 0;
        for (i = 0; i < points; i++) {
            unsigned char f = flags[i];

            if ((f & 4u) != 0u) {
                if (!fits(t, p, 1u)) {
                    return 0;
                }
                v += ((f & 32u) != 0u) ? (int)t->data[p] : -(int)t->data[p];
                p += 1u;
            } else if ((f & 32u) == 0u) {
                if (!fits(t, p, 2u)) {
                    return 0;
                }
                v += rds16(t->data + p);
                p += 2u;
            }
            out->pt[base + i].y = (float)v + dy;
        }
    }
    out->n = base + points;
    out->contours += contours;
    return 1;
}

/** @brief A composite glyph: other glyphs, placed. */
static int
read_composite(const tiku_desk_ttf_t *t, size_t off, size_t len,
               ttf_outline_t *out, float dx, float dy, int depth)
{
    size_t p = off + 10u;
    size_t end = off + len;

    for (;;) {
        unsigned flags, index;
        float ax = 0.0f, ay = 0.0f;

        if (p + 4u > end || !fits(t, p, 4u)) {
            return 1;
        }
        flags = rd16(t->data + p);
        index = rd16(t->data + p + 2);
        p += 4u;
        if ((flags & 1u) != 0u) {       /* words */
            if (!fits(t, p, 4u)) {
                return 1;
            }
            ax = (float)rds16(t->data + p);
            ay = (float)rds16(t->data + p + 2);
            p += 4u;
        } else {
            if (!fits(t, p, 2u)) {
                return 1;
            }
            ax = (float)(signed char)t->data[p];
            ay = (float)(signed char)t->data[p + 1];
            p += 2u;
        }
        /* Scaled components are placed but not scaled: a scaled accent is
         * rare, and drawing it at its own size beats not drawing it. */
        if ((flags & 8u) != 0u) {
            p += 2u;
        } else if ((flags & 0x40u) != 0u) {
            p += 4u;
        } else if ((flags & 0x80u) != 0u) {
            p += 8u;
        }
        if ((flags & 2u) == 0u) {       /* point matching, not offsets */
            ax = 0.0f;
            ay = 0.0f;
        }
        (void)read_outline(t, index, out, dx + ax, dy + ay, depth + 1);
        if ((flags & 0x20u) == 0u) {
            return 1;
        }
    }
}

static int
read_outline(const tiku_desk_ttf_t *t, unsigned g, ttf_outline_t *out,
             float dx, float dy, int depth)
{
    size_t off = 0, len;
    int contours;

    if (depth > TTF_MAX_DEPTH) {
        return 0;
    }
    len = glyph_at(t, g, &off);
    if (len < 10u) {
        return 0;               /* no outline: a space has none */
    }
    contours = rds16(t->data + off);
    if (contours >= 0) {
        return read_simple(t, off, len, contours, out, dx, dy);
    }
    return read_composite(t, off, len, out, dx, dy, depth);
}

/* --------------------------------------------------------------- hinting */

/** @brief How high glyph @p cp reaches, in font units.  0 when unknown. */
static float
glyph_top(const tiku_desk_ttf_t *t, unsigned cp)
{
    unsigned g = glyph_index(t, cp);
    size_t off = 0;

    if (g == 0u || t->cff != NULL) {
        return 0.0f;            /* charstrings carry no bounding box */
    }
    if (glyph_at(t, g, &off) < 10u) {
        return 0.0f;
    }
    return (float)rds16(t->data + off + 8);      /* yMax */
}

/**
 * @brief Find the x-height and cap height, once.
 *
 * The font's own OS/2 table states both, from version 2 on, and that is
 * the designer's answer rather than ours.  Failing that we measure the
 * two letters everyone measures.
 */
static void
measure_zones(tiku_desk_ttf_t *t)
{
    if (t->zones) {
        return;
    }
    t->zones = 1;
    if (t->os2 != 0 && fits(t, t->os2, 90u) &&
        rd16(t->data + t->os2) >= 2u) {
        t->zone_x = (float)rds16(t->data + t->os2 + 86);
        t->zone_cap = (float)rds16(t->data + t->os2 + 88);
    }
    if (t->zone_x <= 0.0f) {
        t->zone_x = glyph_top(t, 'x');
    }
    if (t->zone_cap <= 0.0f) {
        t->zone_cap = glyph_top(t, 'H');
    }
    /* A face whose zones we could not find -- a symbol font, a script
     * with no such lines -- is drawn unhinted rather than wrongly. */
    if (t->zone_x <= 0.0f || t->zone_cap <= t->zone_x) {
        t->zone_x = 0.0f;
        t->zone_cap = 0.0f;
    }
}

/** @brief The transform for @p px: scaled, and snapped where it counts. */
static void
build_hint(tiku_desk_ttf_t *t, float scale, tiku_desk_hint_t *hint)
{
    const char *off = getenv("TIKU_DESK_HINT");

    memset(hint, 0, sizeof *hint);
    hint->scale = scale;
    if (off != NULL && off[0] == '0') {
        return;                 /* an escape hatch, and how we measure it */
    }
    measure_zones(t);
    if (t->zone_x > 0.0f) {
        float dx = t->zone_x * scale;
        float dc = t->zone_cap * scale;

        /* Round the top of the lowercase and the top of the capitals to
         * whole pixels; everything between the baseline and them comes
         * along proportionally, and x is untouched throughout. */
        hint->from[0] = t->zone_x;
        hint->shift[0] = (float)(int)(dx + 0.5f) - dx;
        hint->zones = 1;
        /*
         * The two lines are only worth treating as two if there is room
         * between them.  Nudging a pair a third of a pixel apart in
         * OPPOSITE directions stretches everything caught between by
         * whatever ratio the gap happens to be, which wrecks the letters
         * that live there -- accents especially, which sit exactly in
         * that band.  Close together, one line stands for both.
         */
        if (dc - dx >= 1.0f) {
            float other = (float)(int)(dc + 0.5f) - dc;
            float stretch = (other - hint->shift[0]) / (dc - dx);

            /*
             * And only if the band between them keeps its proportions.
             * The gap being a pixel wide is not enough: nudging the two
             * ends a half pixel APART stretches everything in between by
             * half again, and what lives in between is the accents.
             */
            if (stretch < 0.25f && stretch > -0.25f) {
                hint->from[1] = t->zone_cap;
                hint->shift[1] = other;
                hint->zones = 2;
            }
        }
    }
}

/* -------------------------------------------------------------- emitting */

/**
 * @brief Push one contour into @p path, in device space.
 *
 * TrueType leaves the on-curve point between two off-curve ones implied,
 * and a contour may open on an off-curve point, so the start has to be
 * found before the walk rather than assumed.
 */
static void
emit_contour(tiku_desk_path_t *path, const ttf_point_t *p, int n,
             const tiku_desk_hint_t *hint)
{
    float sx, sy, cx = 0.0f, cy = 0.0f;
    int i, have_ctrl = 0;

    if (n < 2) {
        return;
    }
    if (p[0].on) {
        sx = p[0].x;
        sy = p[0].y;
        i = 1;
    } else if (p[n - 1].on) {
        sx = p[n - 1].x;
        sy = p[n - 1].y;
        i = 0;
    } else {
        sx = 0.5f * (p[0].x + p[n - 1].x);
        sy = 0.5f * (p[0].y + p[n - 1].y);
        i = 0;
    }
    tiku_desk_path_move(path, tiku_desk_hint_x(hint, sx),
                        tiku_desk_hint_y(hint, sy));
    for (; i <= n; i++) {
        const ttf_point_t *q = &p[i % n];
        float qx = (i == n) ? sx : q->x;
        float qy = (i == n) ? sy : q->y;
        int on = (i == n) ? 1 : q->on;

        if (!on) {
            if (have_ctrl) {
                /* Two controls in a row: the midpoint between them is a
                 * point on the curve, and the format leaves it out. */
                tiku_desk_path_quad(path, tiku_desk_hint_x(hint, cx),
                                    tiku_desk_hint_y(hint, cy),
                                    tiku_desk_hint_x(hint,
                                                     0.5f * (cx + qx)),
                                    tiku_desk_hint_y(hint,
                                                     0.5f * (cy + qy)));
            }
            cx = qx;
            cy = qy;
            have_ctrl = 1;
            continue;
        }
        if (have_ctrl) {
            tiku_desk_path_quad(path, tiku_desk_hint_x(hint, cx),
                                tiku_desk_hint_y(hint, cy),
                                tiku_desk_hint_x(hint, qx),
                                tiku_desk_hint_y(hint, qy));
            have_ctrl = 0;
        } else {
            tiku_desk_path_line(path, tiku_desk_hint_x(hint, qx),
                                tiku_desk_hint_y(hint, qy));
        }
    }
    tiku_desk_path_close(path);
}

/** @brief Push a whole glyph's contours into @p path. */
static void
emit_outline(tiku_desk_path_t *path, const ttf_outline_t *outline,
             const tiku_desk_hint_t *hint)
{
    int i, start = 0;

    for (i = 0; i < outline->contours; i++) {
        int end = outline->end[i];
        int n = end - start + 1;

        if (n > 1 && end < outline->n) {
            emit_contour(path, &outline->pt[start], n, hint);
        }
        start = end + 1;
    }
}

/* ------------------------------------------------------------------- API */

int
tiku_desk_ttf_is_font(const char *path)
{
    unsigned char head[4];
    FILE *f;
    size_t n;

    if (path == NULL) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(head, 1u, sizeof head, f);
    (void)fclose(f);
    if (n != sizeof head) {
        return 0;
    }
    /* Judged by what it holds: 1.0 outlines, Apple's "true", a
     * collection, or "OTTO" -- the same file with the other kind of
     * outline in it. */
    return (rd32(head) == 0x00010000ul ||
            memcmp(head, "true", 4) == 0 ||
            memcmp(head, "ttcf", 4) == 0 ||
            memcmp(head, "OTTO", 4) == 0);
}

tiku_desk_ttf_t *
tiku_desk_ttf_open(const char *path)
{
    tiku_desk_ttf_t *t;
    FILE *f;
    long size;
    size_t dir = 0, head, hhea, maxp, name, cmap, os2;

    if (path == NULL) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        fseek(f, 0L, SEEK_SET) != 0) {
        (void)fclose(f);
        return NULL;
    }
    t = calloc(1u, sizeof *t);
    if (t == NULL) {
        (void)fclose(f);
        return NULL;
    }
    t->data = malloc((size_t)size);
    if (t->data == NULL || fread(t->data, 1u, (size_t)size, f) !=
                           (size_t)size) {
        (void)fclose(f);
        tiku_desk_ttf_close(t);
        return NULL;
    }
    (void)fclose(f);
    t->len = (size_t)size;

    if (fits(t, 0u, 12u) && memcmp(t->data, "ttcf", 4) == 0) {
        /* A collection: the first face in it, which is the one a person
         * dropping "the font" in a folder means. */
        if (!fits(t, 12u, 4u)) {
            tiku_desk_ttf_close(t);
            return NULL;
        }
        dir = (size_t)rd32(t->data + 12);
    }
    if (!fits(t, dir, 12u)) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
    if (rd32(t->data + dir) != 0x00010000ul &&
        memcmp(t->data + dir, "true", 4) != 0 &&
        memcmp(t->data + dir, "OTTO", 4) != 0) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
    head = table_of(t, dir, "head", NULL);
    hhea = table_of(t, dir, "hhea", NULL);
    maxp = table_of(t, dir, "maxp", NULL);
    name = table_of(t, dir, "name", NULL);
    cmap = table_of(t, dir, "cmap", NULL);
    os2 = table_of(t, dir, "OS/2", NULL);
    t->hmtx = table_of(t, dir, "hmtx", &t->hmtx_len);
    t->loca = table_of(t, dir, "loca", &t->loca_len);
    t->glyf = table_of(t, dir, "glyf", &t->glyf_len);
    {
        /* The table's tag has a trailing space, which is easy to lose.
         * CFF2 is the same outlines left variable: we draw its default
         * instance, and it is opened by the same reader. */
        size_t cff_len = 0;
        size_t cff_off = table_of(t, dir, "CFF ", &cff_len);

        if (cff_off == 0 || cff_len == 0) {
            cff_off = table_of(t, dir, "CFF2", &cff_len);
        }
        if (cff_off != 0 && cff_len != 0) {
            t->cff = tiku_desk_cff_open(t->data + cff_off, cff_len);
            t->cff_upem = tiku_desk_cff_upem(t->cff);
        }
    }
    if (head == 0 || hhea == 0 || maxp == 0 || t->hmtx == 0 ||
        !fits(t, head, 54u) || !fits(t, hhea, 36u) || !fits(t, maxp, 6u)) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
    if (t->cff == NULL && (t->glyf == 0 || t->loca == 0)) {
        tiku_desk_ttf_close(t);
        return NULL;            /* no outlines of either kind */
    }
    t->upem = (int)rd16(t->data + head + 18);
    t->long_loca = (rds16(t->data + head + 50) != 0);
    t->ascent = rds16(t->data + hhea + 4);
    t->descent = rds16(t->data + hhea + 6);
    t->num_hmetrics = (int)rd16(t->data + hhea + 34);
    t->num_glyphs = (int)rd16(t->data + maxp + 4);
    if (t->upem < 16 || t->upem > 16384 || t->num_glyphs <= 0) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
    t->os2 = os2;
    if (os2 != 0 && fits(t, os2, 64u)) {
        t->bold = (rd16(t->data + os2 + 62) & 32u) != 0u;   /* fsSelection */
    } else if (fits(t, head, 46u)) {
        t->bold = (rd16(t->data + head + 44) & 1u) != 0u;   /* macStyle */
    }
    read_family(t, name);
    read_cmap(t, cmap);
    if (t->family[0] == '\0') {
        const char *leaf = strrchr(path, '/');

        snprintf(t->family, sizeof t->family, "%s",
                 (leaf != NULL) ? leaf + 1 : path);
    }
    return t;
}

void
tiku_desk_ttf_close(tiku_desk_ttf_t *ttf)
{
    if (ttf != NULL) {
        tiku_desk_cff_close(ttf->cff);
        free(ttf->data);
        free(ttf);
    }
}

const char *
tiku_desk_ttf_family(const tiku_desk_ttf_t *ttf)
{
    return (ttf != NULL) ? ttf->family : NULL;
}

int
tiku_desk_ttf_bold(const tiku_desk_ttf_t *ttf)
{
    return (ttf != NULL) ? ttf->bold : 0;
}

void
tiku_desk_ttf_metrics(const tiku_desk_ttf_t *ttf, int px, int *ascent,
                      int *height)
{
    float scale;

    if (ttf == NULL || px <= 0) {
        return;
    }
    scale = (float)px / (float)ttf->upem;
    if (ascent != NULL) {
        *ascent = (int)((float)ttf->ascent * scale + 0.5f);
    }
    if (height != NULL) {
        *height = (int)((float)(ttf->ascent - ttf->descent) * scale + 0.5f);
    }
}

int
tiku_desk_ttf_has(const tiku_desk_ttf_t *ttf, unsigned cp)
{
    return ttf != NULL && glyph_index(ttf, cp) != 0u;
}

int
tiku_desk_ttf_render(const tiku_desk_ttf_t *ttf, unsigned cp, int px,
                     tiku_desk_ttf_glyph_t *out)
{
    static ttf_outline_t outline;       /* 48 KB: not on the stack */
    tiku_desk_hint_t hint;
    tiku_desk_path_t *path;
    unsigned g;
    float scale;
    int x0, y0, w, h;

    if (ttf == NULL || out == NULL || px <= 0 || px > TTF_MAX_SIZE) {
        return 0;
    }
    g = glyph_index(ttf, cp);
    if (g == 0u && cp != 0u) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    scale = (float)px / (float)ttf->upem;
    out->adv = (int)((float)advance_of(ttf, g) * scale + 0.5f);

    if (ttf->cff == NULL) {
        outline.n = 0;
        outline.contours = 0;
        if (!read_outline(ttf, g, &outline, 0.0f, 0.0f, 0) ||
            outline.n == 0) {
            return 1;           /* a space: an advance and no ink */
        }
    }
    path = tiku_desk_path_new();
    if (path == NULL) {
        return 0;
    }
    if (ttf->cff != NULL) {
        /* The charstrings have a unit of their own, which the FontMatrix
         * states and which need not be the sfnt's. */
        build_hint((tiku_desk_ttf_t *)ttf, (float)px /
                   ((ttf->cff_upem > 0.0f) ? ttf->cff_upem
                                           : (float)ttf->upem), &hint);
        if (!tiku_desk_cff_outline(ttf->cff, g, &hint, path)) {
            tiku_desk_path_free(path);
            return 1;
        }
    } else {
        build_hint((tiku_desk_ttf_t *)ttf, scale, &hint);
        emit_outline(path, &outline, &hint);
    }
    tiku_desk_path_bounds(path, &x0, &y0, &w, &h);
    if (tiku_desk_path_failed(path) || w <= 0 || h <= 0) {
        tiku_desk_path_free(path);
        return 1;               /* nothing drawable: an advance and no ink */
    }
    {
        /* The same switch the grid-fit answers to turns the darkening
         * off as well, so TIKU_DESK_HINT=0 gives the honest linear
         * coverage for measuring against. */
        const char *off = getenv("TIKU_DESK_HINT");
        int darken = !(off != NULL && off[0] == '0');

        out->cover = tiku_desk_path_render(path, x0, y0, w, h, darken);
    }
    tiku_desk_path_free(path);
    if (out->cover == NULL) {
        return 0;
    }
    out->w = w;
    out->h = h;
    out->ox = x0;
    out->oy = y0;
    return 1;
}

void
tiku_desk_ttf_free_glyph(tiku_desk_ttf_glyph_t *glyph)
{
    if (glyph != NULL) {
        free(glyph->cover);
        glyph->cover = NULL;
    }
}
