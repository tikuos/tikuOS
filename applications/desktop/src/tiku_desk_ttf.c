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

/* ------------------------------------------------------------ rasterising */

/*
 * Signed area, accumulated per cell and resolved with a prefix sum -- the
 * same answer the icon renderer reaches for its paths, and for the same
 * reason: coverage that is computed rather than sampled has no jaggies to
 * average away, which matters most at the sizes an interface uses.
 */
typedef struct {
    float *a;
    int    w, h;
} ttf_raster_t;

static void
raster_line(ttf_raster_t *r, float x0, float y0, float x1, float y1)
{
    float dir = 1.0f, dxdy, x;
    int y, ylast;

    if (y0 == y1) {
        return;
    }
    if (y0 > y1) {
        float t;

        dir = -1.0f;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    dxdy = (x1 - x0) / (y1 - y0);
    x = x0;
    y = (int)y0;
    if (y < 0) {
        x -= y0 * dxdy;
        y = 0;
    }
    ylast = (int)(y1 + 0.9999f);
    if (ylast > r->h) {
        ylast = r->h;
    }
    for (; y < ylast; y++) {
        float top = ((float)y > y0) ? (float)y : y0;
        float bot = ((float)(y + 1) < y1) ? (float)(y + 1) : y1;
        float dy = bot - top;
        float xnext = x + dxdy * dy;
        float d = dy * dir;
        float xa = (x < xnext) ? x : xnext;
        float xb = (x < xnext) ? xnext : x;
        float xaf = (float)(int)((xa < 0.0f) ? 0.0f : xa);
        int xai = (int)xaf;
        int xbi = (int)(xb + 0.9999f);
        float *row = r->a + (size_t)y * (r->w + 2);

        if (xai < 0) { xai = 0; }
        if (xbi > r->w) { xbi = r->w; }
        if (xai >= r->w) {
            x = xnext;
            continue;
        }
        if (xbi <= xai + 1) {
            float mid = 0.5f * (x + xnext) - xaf;

            if (mid < 0.0f) { mid = 0.0f; }
            if (mid > 1.0f) { mid = 1.0f; }
            row[xai] += d - d * mid;
            row[xai + 1] += d * mid;
        } else {
            /* The span crosses cells: the first and last get the area of
             * their corner triangles, the ones between get a full slice. */
            float s = 1.0f / (xb - xa);
            float xaff = xa - xaf;
            float a0 = 0.5f * s * (1.0f - xaff) * (1.0f - xaff);
            float xbf = xb - (float)xbi + 1.0f;
            float am = 0.5f * s * xbf * xbf;
            int xi;

            row[xai] += d * a0;
            if (xbi == xai + 2) {
                row[xai + 1] += d * (1.0f - a0 - am);
            } else {
                float a1 = s * (1.5f - xaff);
                float a2;

                row[xai + 1] += d * (a1 - a0);
                for (xi = xai + 2; xi < xbi - 1; xi++) {
                    row[xi] += d * s;
                }
                a2 = a1 + (float)(xbi - xai - 3) * s;
                row[xbi - 1] += d * (1.0f - a2 - am);
            }
            row[xbi] += d * am;
        }
        x = xnext;
    }
}

static void
raster_resolve(const ttf_raster_t *r, unsigned char *out)
{
    int x, y;

    for (y = 0; y < r->h; y++) {
        const float *row = r->a + (size_t)y * (r->w + 2);
        float acc = 0.0f;

        for (x = 0; x < r->w; x++) {
            int v;

            acc += row[x];
            v = (int)((acc < 0.0f ? -acc : acc) * 255.0f + 0.5f);
            out[(size_t)y * r->w + x] = (unsigned char)((v > 255) ? 255 : v);
        }
    }
}

/** @brief Walk one contour, flattening its curves into @p r. */
static void
stroke_contour(ttf_raster_t *r, const ttf_point_t *p, int n)
{
    float sx, sy, cx = 0.0f, cy = 0.0f, px, py;
    int i, have_ctrl = 0;

    if (n < 2) {
        return;
    }
    /* A contour may open on an off-curve point, in which case the start
     * is the midpoint the format leaves implied. */
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
    px = sx;
    py = sy;
    for (; i <= n; i++) {
        const ttf_point_t *q = &p[i % n];
        float qx = (i == n) ? sx : q->x;
        float qy = (i == n) ? sy : q->y;
        int on = (i == n) ? 1 : q->on;

        if (!on) {
            if (have_ctrl) {
                float mx = 0.5f * (cx + qx), my = 0.5f * (cy + qy);
                int steps, s;
                float x0 = px, y0 = py;

                steps = 8;
                for (s = 1; s <= steps; s++) {
                    float u = (float)s / (float)steps, v = 1.0f - u;
                    float bx = v * v * x0 + 2.0f * v * u * cx + u * u * mx;
                    float by = v * v * y0 + 2.0f * v * u * cy + u * u * my;

                    raster_line(r, px, py, bx, by);
                    px = bx;
                    py = by;
                }
            }
            cx = qx;
            cy = qy;
            have_ctrl = 1;
            continue;
        }
        if (have_ctrl) {
            int steps = 8, s;
            float x0 = px, y0 = py;

            for (s = 1; s <= steps; s++) {
                float u = (float)s / (float)steps, v = 1.0f - u;
                float bx = v * v * x0 + 2.0f * v * u * cx + u * u * qx;
                float by = v * v * y0 + 2.0f * v * u * cy + u * u * qy;

                raster_line(r, px, py, bx, by);
                px = bx;
                py = by;
            }
            have_ctrl = 0;
        } else {
            raster_line(r, px, py, qx, qy);
            px = qx;
            py = qy;
        }
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
    /* Judged by what it holds: 1.0 outlines, Apple's "true", or a
     * collection.  "OTTO" is a PostScript-outline face, which says so
     * here rather than after a page of parsing. */
    return (rd32(head) == 0x00010000ul ||
            memcmp(head, "true", 4) == 0 ||
            memcmp(head, "ttcf", 4) == 0);
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
        memcmp(t->data + dir, "true", 4) != 0) {
        tiku_desk_ttf_close(t);
        return NULL;    /* OTTO and anything else: not our outlines */
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
    if (head == 0 || hhea == 0 || maxp == 0 || t->glyf == 0 ||
        t->loca == 0 || t->hmtx == 0 ||
        !fits(t, head, 54u) || !fits(t, hhea, 36u) || !fits(t, maxp, 6u)) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
    t->upem = (int)rd16(t->data + head + 18);
    t->long_loca = (rds16(t->data + head + 50) != 0);
    t->ascent = rds16(t->data + hhea + 4);
    t->descent = rds16(t->data + hhea + 6);
    t->num_hmetrics = (int)rd16(t->data + hhea + 34);
    t->num_glyphs = (int)rd16(t->data + maxp + 4);
    if (t->upem <= 0 || t->num_glyphs <= 0) {
        tiku_desk_ttf_close(t);
        return NULL;
    }
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
    unsigned g;
    float scale, minx, miny, maxx, maxy;
    int i, x0, y0, x1, y1, w, h;
    ttf_raster_t r;

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

    outline.n = 0;
    outline.contours = 0;
    if (!read_outline(ttf, g, &outline, 0.0f, 0.0f, 0) || outline.n == 0) {
        return 1;               /* a space: an advance and no ink */
    }
    minx = maxx = outline.pt[0].x;
    miny = maxy = outline.pt[0].y;
    for (i = 1; i < outline.n; i++) {
        if (outline.pt[i].x < minx) { minx = outline.pt[i].x; }
        if (outline.pt[i].x > maxx) { maxx = outline.pt[i].x; }
        if (outline.pt[i].y < miny) { miny = outline.pt[i].y; }
        if (outline.pt[i].y > maxy) { maxy = outline.pt[i].y; }
    }
    /* Device space: y down from the baseline, and a pixel of margin so
     * the accumulator's spill into the next cell has somewhere to go. */
    x0 = (int)((minx * scale) < 0.0f ? (minx * scale) - 1.0f
                                     : (minx * scale)) - 1;
    x1 = (int)(maxx * scale + 0.9999f) + 1;
    y0 = -(int)(maxy * scale + 0.9999f) - 1;
    y1 = -(int)((miny * scale) < 0.0f ? (miny * scale) - 1.0f
                                      : (miny * scale)) + 1;
    w = x1 - x0;
    h = y1 - y0;
    if (w <= 0 || h <= 0 || w > TTF_MAX_SIZE || h > TTF_MAX_SIZE) {
        return 1;
    }
    r.w = w;
    r.h = h;
    r.a = calloc((size_t)(w + 2) * (size_t)h, sizeof *r.a);
    out->cover = calloc((size_t)w * (size_t)h, 1u);
    if (r.a == NULL || out->cover == NULL) {
        free(r.a);
        free(out->cover);
        out->cover = NULL;
        return 0;
    }
    {
        int start = 0;

        for (i = 0; i < outline.contours; i++) {
            int end = outline.end[i];
            int n = end - start + 1;
            ttf_point_t *p = &outline.pt[start];
            int k;

            if (n > 1 && end < outline.n) {
                for (k = 0; k < n; k++) {
                    p[k].x = p[k].x * scale - (float)x0;
                    p[k].y = -(p[k].y * scale) - (float)y0;
                }
                stroke_contour(&r, p, n);
            }
            start = end + 1;
        }
    }
    raster_resolve(&r, out->cover);
    free(r.a);
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
