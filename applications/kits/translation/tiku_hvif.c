/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_hvif.c - HVIF parser and rasteriser.
 *
 * Two halves that share nothing but the icon struct.  The parser walks a
 * strictly sequential byte stream -- styles, paths, shapes, no offsets and
 * no padding -- and must land on the final byte, which is the only
 * end-to-end check the format offers.  The rasteriser accumulates signed
 * edge area into a float buffer and resolves it with a per-row prefix sum,
 * so nonzero winding is one fabsf() and coverage is analytic rather than
 * sampled.
 *
 * Format reimplemented from Haiku's src/libs/icon/flat_icon/ and
 * src/libs/icon/{IconRenderer,style,shape,transformer}/ (Haiku,
 * haiku-os.org, MIT licensed).
 *
 * DELIBERATELY NOT IMPLEMENTED.  Each of these consumes its bytes so the
 * stream stays in sync, then degrades rather than failing:
 *   - CONTOUR (21), AFFINE (20) and PERSPECTIVE (22) transformers.  Zero
 *     occurrences in the Tracker set; AFFINE and PERSPECTIVE have zero in
 *     the whole Haiku tree.  Payload is skipped and the shape draws with
 *     its own matrix only.
 *   - SHAPE_FLAG_HINTING.  Cosmetic vertex snapping; unset in every
 *     Tracker icon.  Axis-aligned edges stay soft at 16 px.
 *   - Gamma-correct blending.  Haiku composites through a 2.2 LUT; without
 *     it antialiased edges read slightly heavier on a light panel.
 *   - The compound rasteriser.  Shapes composite back to front instead of
 *     resolving per scanline, which leaves a faint seam where two opaque
 *     shapes share an edge.
 *   - Miter and bevel joins.  The stroker unions round joins under the
 *     nonzero rule; at the 1-2 px stroke widths this artwork uses the
 *     difference is invisible.
 * GRADIENT_FLAG_16_BIT_COLORS is rejected outright: Haiku never writes it
 * and cannot read it, so guessing a layout would desynchronise the stream.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_hvif.h"

/* ------------------------------------------------------------------ wire */

#define HVIF_STYLE_SOLID          1
#define HVIF_STYLE_GRADIENT       2
#define HVIF_STYLE_SOLID_NO_A     3
#define HVIF_STYLE_SOLID_GRAY     4
#define HVIF_STYLE_SOLID_GRAY_NO_A 5

#define HVIF_GRAD_TRANSFORM    0x02
#define HVIF_GRAD_NO_ALPHA     0x04
#define HVIF_GRAD_16BIT        0x08
#define HVIF_GRAD_GRAYS        0x10

#define HVIF_PATH_CLOSED       0x02
#define HVIF_PATH_COMMANDS     0x04
#define HVIF_PATH_NO_CURVES    0x08

#define HVIF_SHAPE_PATH_SOURCE   10
#define HVIF_SHAPE_TRANSFORM   0x02
#define HVIF_SHAPE_HINTING     0x04
#define HVIF_SHAPE_LOD         0x08
#define HVIF_SHAPE_XFORMERS    0x10
#define HVIF_SHAPE_TRANSLATION 0x20

#define HVIF_XF_AFFINE      20
#define HVIF_XF_CONTOUR     21
#define HVIF_XF_PERSPECTIVE 22
#define HVIF_XF_STROKE      23

/* Gradient kinds, in the order agg's span generators are selected. */
#define HVIF_G_LINEAR   0
#define HVIF_G_CIRCULAR 1
#define HVIF_G_DIAMOND  2
#define HVIF_G_CONIC    3
#define HVIF_G_XY       4
#define HVIF_G_SQRT_XY  5

/* The design square every HVIF coordinate is expressed in. */
#define HVIF_CANVAS 64.0f

/* Flattening tolerance in device pixels; well under the 1/255 quantum. */
#define HVIF_TOL 0.1f

/* -------------------------------------------------------------- structs */

typedef struct {
    uint8_t c[4];            /* straight R,G,B,A */
    float   off;             /* 0..1 */
} hvif_stop_t;

typedef struct {
    int     gradient;
    uint8_t solid[4];        /* straight R,G,B,A when !gradient */
    uint8_t kind;
    float   matrix[6];       /* gradient space -> icon space */
    uint8_t ramp[256][4];    /* straight R,G,B,A */
} hvif_style_t;

typedef struct {
    float x, y;              /* the point itself     */
    float ix, iy;            /* incoming control     */
    float ox, oy;            /* outgoing control     */
} hvif_pt_t;

typedef struct {
    hvif_pt_t *pt;
    int        n;
    int        closed;
} hvif_path_t;

typedef struct {
    uint8_t style;
    uint8_t npath;
    uint8_t path[255];
    float   matrix[6];
    int     lod;
    float   lod_min, lod_max;
    int     stroke;
    float   stroke_w;        /* design units */
    uint8_t stroke_cap;      /* 0 butt, 1 square, 2 round */
    int     ok;              /* 0 = malformed record, drawn as nothing */
} hvif_shape_t;

struct tiku_hvif {
    hvif_style_t *style;
    hvif_path_t  *path;
    hvif_shape_t *shape;
    int           nstyle, npath, nshape;
};

/* ------------------------------------------------------------- the reader */

typedef struct {
    const uint8_t *p;
    size_t         len;
    size_t         pos;
    int            bad;
} hvif_rd_t;

static uint8_t
hvif_u8(hvif_rd_t *r)
{
    if (r->pos >= r->len) {
        r->bad = 1;
        return 0;
    }
    return r->p[r->pos++];
}

static uint16_t
hvif_u16(hvif_rd_t *r)
{
    uint16_t lo = hvif_u8(r);
    uint16_t hi = hvif_u8(r);

    return (uint16_t)(lo | (hi << 8));       /* the one little-endian field */
}

/** @brief Copy @p n bytes out, or mark the stream bad and copy nothing. */
static void
hvif_read(hvif_rd_t *r, void *dst, size_t n)
{
    if (r->len - r->pos < n) {
        r->bad = 1;
        r->pos = r->len;
        return;
    }
    memcpy(dst, r->p + r->pos, n);
    r->pos += n;
}

static void
hvif_skip(hvif_rd_t *r, size_t n)
{
    if (r->len - r->pos < n) {
        r->bad = 1;
        r->pos = r->len;
        return;
    }
    r->pos += n;
}

/**
 * @brief Read one coordinate in canvas units.
 *
 * The short form covers the integers -32..95; the long form is 15 bits big
 * endian inside its own two bytes, divided by 102 -- not fixed point.
 */
static float
hvif_coord(hvif_rd_t *r)
{
    uint8_t v = hvif_u8(r);
    uint8_t lo;

    if ((v & 0x80u) == 0u) {
        return (float)v - 32.0f;
    }
    lo = hvif_u8(r);
    return (float)(((uint32_t)(v & 0x7Fu) << 8) | lo) / 102.0f - 128.0f;
}

/**
 * @brief Read a 24-bit float: 1 sign, 6 exponent (bias 32), 17 mantissa.
 *
 * No infinities, NaNs or denormals exist in the encoding; only the all-zero
 * word means zero.
 */
static float
hvif_f24(hvif_rd_t *r)
{
    uint32_t b0 = hvif_u8(r), b1 = hvif_u8(r), b2 = hvif_u8(r);
    uint32_t sv = (b0 << 16) | (b1 << 8) | b2;
    uint32_t sign, bits;
    int exp;
    float out;

    if (sv == 0u) {
        return 0.0f;
    }
    sign = (sv >> 23) & 1u;
    exp  = (int)((sv >> 17) & 0x3Fu) - 32;
    bits = (sign << 31) | ((uint32_t)(exp + 127) << 23) |
           ((sv & 0x1FFFFu) << 6);
    memcpy(&out, &bits, sizeof out);
    return out;
}

static void
hvif_matrix(hvif_rd_t *r, float *m)
{
    int i;

    for (i = 0; i < 6; i++) {
        m[i] = hvif_f24(r);
    }
}

/** @brief Consume an unknown record: a uint16 length, then that many bytes. */
static void
hvif_skip_tagged(hvif_rd_t *r)
{
    hvif_skip(r, hvif_u16(r));
}

/* ---------------------------------------------------------------- styles */

/** @brief Haiku's ease-in-out; HVIF cannot encode linear stops. */
static float
hvif_gauss(float x)
{
    if (x <= 0.0f) {
        return 1.0f;
    }
    if (x >= 1.0f) {
        return 0.0f;
    }
    if (x < 0.5f) {
        return 1.0f - 2.0f * x * x;
    }
    return 2.0f * (1.0f - x) * (1.0f - x);
}

/**
 * @brief Bake stops into 256 straight RGBA entries.
 *
 * Mirrors Gradient::MakeGradient: stops are not assumed sorted, so the
 * lowest offset is searched for and each following stop is the nearest one
 * ahead.  A gradient with no stops is legal on the wire and leaves Haiku's
 * ramp uninitialised; here it is transparent.
 */
static void
hvif_ramp(uint8_t ramp[256][4], const hvif_stop_t *st, int n)
{
    unsigned char used[256];
    int idx, i, k, ch, from = -1;

    if (n <= 0) {
        memset(ramp, 0, 256 * 4);
        return;
    }
    memset(used, 0, sizeof used);
    for (i = 0; i < n; i++) {
        if (from < 0 || st[i].off < st[from].off) {
            from = i;
        }
    }
    used[from] = 1;
    idx = (int)floorf(256.0f * st[from].off + 0.5f);
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 256) {
        idx = 256;
    }
    for (i = 0; i < idx; i++) {
        memcpy(ramp[i], st[from].c, 4);
    }
    for (;;) {
        int to = -1, off, dist;

        for (k = 0; k < n; k++) {
            if (used[k] || st[k].off < st[from].off) {
                continue;
            }
            if (to < 0 || st[k].off < st[to].off) {
                to = k;
            }
        }
        if (to < 0) {
            break;
        }
        used[to] = 1;
        off = (int)floorf(255.0f * st[to].off + 0.5f);
        if (off > 255) {
            off = 255;
        }
        dist = off - idx;
        if (dist >= 0) {
            for (i = idx; i <= off; i++) {
                float f = (float)(off - i) / (float)(dist + 1);
                float t;

                f = hvif_gauss(1.0f - f);
                t = 1.0f - f;
                for (ch = 0; ch < 4; ch++) {
                    ramp[i][ch] = (uint8_t)floorf((float)st[from].c[ch] * f +
                                                  (float)st[to].c[ch] * t +
                                                  0.5f);
                }
            }
            idx = off + 1;
        }
        from = to;
    }
    for (i = idx; i < 256; i++) {
        memcpy(ramp[i], st[from].c, 4);
    }
}

/** @brief Decode one colour, sized by the no-alpha and grays flags. */
static void
hvif_color(hvif_rd_t *r, uint8_t *c, int no_alpha, int gray)
{
    if (gray) {
        c[0] = c[1] = c[2] = hvif_u8(r);
    } else {
        c[0] = hvif_u8(r);
        c[1] = hvif_u8(r);
        c[2] = hvif_u8(r);
    }
    c[3] = no_alpha ? 255u : hvif_u8(r);
}

static int
hvif_read_gradient(hvif_rd_t *r, hvif_style_t *s, char *err, size_t errlen)
{
    hvif_stop_t st[255];
    uint8_t flags;
    int n, i;

    s->gradient = 1;
    s->kind     = hvif_u8(r);
    flags       = hvif_u8(r);
    n           = hvif_u8(r);
    if ((flags & HVIF_GRAD_16BIT) != 0u) {
        snprintf(err, errlen, "gradient asks for 16-bit stops");
        return -1;
    }
    if ((flags & HVIF_GRAD_TRANSFORM) != 0u) {
        hvif_matrix(r, s->matrix);
    }
    for (i = 0; i < n; i++) {
        st[i].off = (float)hvif_u8(r) / 255.0f;
        hvif_color(r, st[i].c, (flags & HVIF_GRAD_NO_ALPHA) != 0u,
                   (flags & HVIF_GRAD_GRAYS) != 0u);
    }
    if (r->bad) {
        snprintf(err, errlen, "truncated gradient");
        return -1;
    }
    hvif_ramp(s->ramp, st, n);
    return 0;
}

static int
hvif_read_styles(hvif_rd_t *r, struct tiku_hvif *ic, char *err,
                 size_t errlen)
{
    int n = hvif_u8(r), i;

    ic->style = calloc((size_t)(n > 0 ? n : 1), sizeof *ic->style);
    if (ic->style == NULL) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    ic->nstyle = n;
    for (i = 0; i < n; i++) {
        hvif_style_t *s = &ic->style[i];
        uint8_t type;

        s->matrix[0] = s->matrix[3] = 1.0f;
        type = hvif_u8(r);
        switch (type) {
        case HVIF_STYLE_SOLID:
            hvif_color(r, s->solid, 0, 0);
            break;
        case HVIF_STYLE_SOLID_NO_A:
            hvif_color(r, s->solid, 1, 0);
            break;
        case HVIF_STYLE_SOLID_GRAY:
            hvif_color(r, s->solid, 0, 1);
            break;
        case HVIF_STYLE_SOLID_GRAY_NO_A:
            hvif_color(r, s->solid, 1, 1);
            break;
        case HVIF_STYLE_GRADIENT:
            if (hvif_read_gradient(r, s, err, errlen) != 0) {
                return -1;
            }
            break;
        default:
            /* Unknown style: skip its tagged length and leave a fully
             * transparent placeholder, so every later style index still
             * lands on the style the file meant.  Haiku drops the slot
             * instead and shifts the rest -- a bug no shipped icon
             * reaches, and not one worth reproducing. */
            hvif_skip_tagged(r);
            break;
        }
        if (r->bad) {
            snprintf(err, errlen, "truncated style %d", i);
            return -1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------- paths */

/**
 * @brief Drop degenerate points, as VectorPath::CleanUp does.
 *
 * Zero-length segments are harmless to a fill but spawn stray caps and
 * joins once the shape carries a stroke.
 */
static void
hvif_path_cleanup(hvif_path_t *p)
{
    int i, w;

    if (p->n >= 2 && p->closed &&
        p->pt[0].x == p->pt[p->n - 1].x && p->pt[0].y == p->pt[p->n - 1].y) {
        p->pt[0].ix = p->pt[p->n - 1].ix;
        p->pt[0].iy = p->pt[p->n - 1].iy;
        p->n--;
    }
    w = 0;
    for (i = 0; i < p->n; i++) {
        if (i + 1 < p->n) {
            const hvif_pt_t *a = &p->pt[i], *b = &p->pt[i + 1];

            if (a->x == b->x && a->y == b->y &&
                a->x == a->ox && a->y == a->oy &&
                b->x == b->ix && b->y == b->iy) {
                p->pt[i + 1].ix = a->ix;
                p->pt[i + 1].iy = a->iy;
                continue;               /* a is absorbed into b */
            }
        }
        p->pt[w++] = p->pt[i];
    }
    p->n = w;
}

static int
hvif_read_paths(hvif_rd_t *r, struct tiku_hvif *ic, char *err,
                size_t errlen)
{
    int n = hvif_u8(r), i;

    ic->path = calloc((size_t)(n > 0 ? n : 1), sizeof *ic->path);
    if (ic->path == NULL) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    ic->npath = n;
    for (i = 0; i < n; i++) {
        hvif_path_t *p = &ic->path[i];
        uint8_t flags = hvif_u8(r);
        int count = hvif_u8(r), k;

        p->closed = (flags & HVIF_PATH_CLOSED) != 0u;
        p->n = count;
        p->pt = calloc((size_t)(count > 0 ? count : 1), sizeof *p->pt);
        if (p->pt == NULL) {
            snprintf(err, errlen, "out of memory");
            return -1;
        }
        if ((flags & HVIF_PATH_NO_CURVES) != 0u) {
            for (k = 0; k < count; k++) {
                p->pt[k].x = hvif_coord(r);
                p->pt[k].y = hvif_coord(r);
                p->pt[k].ix = p->pt[k].ox = p->pt[k].x;
                p->pt[k].iy = p->pt[k].oy = p->pt[k].y;
            }
        } else if ((flags & HVIF_PATH_COMMANDS) != 0u) {
            /* Two bits per point, packed LSB-first, and the point count is
             * a single byte -- so the block is ceil(count / 4) bytes and
             * can never outrun 64.  Read it before the coordinates: the
             * coordinate stream is variable width and cannot be indexed. */
            uint8_t cmd[64];
            size_t nc = ((size_t)count + 3u) / 4u;
            float px = 0.0f, py = 0.0f;

            _Static_assert(sizeof cmd >= (255 + 3) / 4, "command block cap");
            hvif_read(r, cmd, nc);
            if (r->bad) {
                break;          /* cmd holds nothing; do not decode from it */
            }
            for (k = 0; k < count; k++) {
                int op = (cmd[k >> 2] >> ((k & 3) * 2)) & 0x03;

                switch (op) {
                case 0:                            /* H_LINE */
                    p->pt[k].x = hvif_coord(r);
                    p->pt[k].y = py;
                    break;
                case 1:                            /* V_LINE */
                    p->pt[k].x = px;
                    p->pt[k].y = hvif_coord(r);
                    break;
                case 2:                            /* LINE */
                    p->pt[k].x = hvif_coord(r);
                    p->pt[k].y = hvif_coord(r);
                    break;
                default:                           /* CURVE */
                    p->pt[k].x  = hvif_coord(r);
                    p->pt[k].y  = hvif_coord(r);
                    p->pt[k].ix = hvif_coord(r);
                    p->pt[k].iy = hvif_coord(r);
                    p->pt[k].ox = hvif_coord(r);
                    p->pt[k].oy = hvif_coord(r);
                    break;
                }
                if (op != 3) {
                    p->pt[k].ix = p->pt[k].ox = p->pt[k].x;
                    p->pt[k].iy = p->pt[k].oy = p->pt[k].y;
                }
                px = p->pt[k].x;
                py = p->pt[k].y;
            }
        } else {
            for (k = 0; k < count; k++) {
                p->pt[k].x  = hvif_coord(r);
                p->pt[k].y  = hvif_coord(r);
                p->pt[k].ix = hvif_coord(r);
                p->pt[k].iy = hvif_coord(r);
                p->pt[k].ox = hvif_coord(r);
                p->pt[k].oy = hvif_coord(r);
            }
        }
        if (r->bad) {
            snprintf(err, errlen, "truncated path %d", i);
            return -1;
        }
        hvif_path_cleanup(p);
    }
    return 0;
}

/* ---------------------------------------------------------------- shapes */

static int
hvif_read_transformers(hvif_rd_t *r, hvif_shape_t *sh)
{
    int n = hvif_u8(r), i;

    for (i = 0; i < n; i++) {
        uint8_t type = hvif_u8(r);

        switch (type) {
        case HVIF_XF_STROKE: {
            uint8_t width = hvif_u8(r);
            uint8_t opts  = hvif_u8(r);

            (void)hvif_u8(r);                      /* miter limit */
            sh->stroke     = 1;
            sh->stroke_w   = (float)width - 128.0f;
            sh->stroke_cap = (uint8_t)(opts >> 4);
            break;
        }
        case HVIF_XF_CONTOUR:
            hvif_skip(r, 3);
            break;
        case HVIF_XF_AFFINE:
            /* 18 bytes: the exporter writes six float24s.  Haiku's own
             * importer reads six float32s and desynchronises, which is why
             * no file in the tree carries one. */
            hvif_skip(r, 18);
            break;
        case HVIF_XF_PERSPECTIVE:
            hvif_skip(r, 27);
            break;
        default:
            hvif_skip_tagged(r);
            break;
        }
        if (r->bad) {
            return -1;
        }
    }
    return 0;
}

static int
hvif_read_shapes(hvif_rd_t *r, struct tiku_hvif *ic, char *err,
                 size_t errlen)
{
    int n = hvif_u8(r), i;

    ic->shape = calloc((size_t)(n > 0 ? n : 1), sizeof *ic->shape);
    if (ic->shape == NULL) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    ic->nshape = n;
    for (i = 0; i < n; i++) {
        hvif_shape_t *sh = &ic->shape[i];
        uint8_t type = hvif_u8(r);
        uint8_t flags;
        int k;

        sh->matrix[0] = sh->matrix[3] = 1.0f;
        if (type != HVIF_SHAPE_PATH_SOURCE) {
            hvif_skip_tagged(r);           /* leaves sh->ok == 0: no draw */
            if (r->bad) {
                snprintf(err, errlen, "truncated shape %d", i);
                return -1;
            }
            continue;
        }
        sh->ok    = 1;
        sh->style = hvif_u8(r);
        sh->npath = hvif_u8(r);
        for (k = 0; k < sh->npath; k++) {
            sh->path[k] = hvif_u8(r);
        }
        flags = hvif_u8(r);
        /* TRANSFORM and TRANSLATION are an if/else-if: with both set only
         * the matrix is present, and reading both loses the stream. */
        if ((flags & HVIF_SHAPE_TRANSFORM) != 0u) {
            hvif_matrix(r, sh->matrix);
        } else if ((flags & HVIF_SHAPE_TRANSLATION) != 0u) {
            sh->matrix[4] = hvif_coord(r);
            sh->matrix[5] = hvif_coord(r);
        }
        if ((flags & HVIF_SHAPE_LOD) != 0u) {
            sh->lod     = 1;
            sh->lod_min = (float)hvif_u8(r) / 63.75f;
            sh->lod_max = (float)hvif_u8(r) / 63.75f;
        }
        if ((flags & HVIF_SHAPE_XFORMERS) != 0u) {
            if (hvif_read_transformers(r, sh) != 0) {
                snprintf(err, errlen, "truncated transformers in shape %d", i);
                return -1;
            }
        }
        if (r->bad) {
            snprintf(err, errlen, "truncated shape %d", i);
            return -1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------- parse */

tiku_hvif_t *
tiku_hvif_parse(const void *data, size_t len, char *err, size_t errlen)
{
    static const uint8_t magic[4] = { 0x6E, 0x63, 0x69, 0x66 };   /* "ncif" */
    char scratch[TIKU_HVIF_ERRLEN];
    struct tiku_hvif *ic;
    hvif_rd_t r;

    if (err == NULL || errlen == 0) {
        err = scratch;
        errlen = sizeof scratch;
    }
    err[0] = '\0';
    if (data == NULL || len < 5u) {
        snprintf(err, errlen, "blob too short");
        return NULL;
    }
    if (memcmp(data, magic, sizeof magic) != 0) {
        snprintf(err, errlen, "not HVIF: bad magic");
        return NULL;
    }
    ic = calloc(1, sizeof *ic);
    if (ic == NULL) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    r.p = data;
    r.len = len;
    r.pos = 4;
    r.bad = 0;
    if (hvif_read_styles(&r, ic, err, errlen) != 0 ||
        hvif_read_paths(&r, ic, err, errlen) != 0 ||
        hvif_read_shapes(&r, ic, err, errlen) != 0) {
        tiku_hvif_free(ic);
        return NULL;
    }
    /* A read that ran off the end leaves pos where it was, so the
     * consumed-everything test alone would pass a blob truncated exactly at a
     * section's count byte: the count reads as zero, the section loop never
     * runs, and an empty icon looks structurally valid. */
    if (r.bad || r.pos != len) {
        snprintf(err, errlen, r.bad ? "truncated" : "%zu of %zu bytes consumed",
                 r.pos, len);
        tiku_hvif_free(ic);
        return NULL;
    }
    return ic;
}

void
tiku_hvif_free(tiku_hvif_t *icon)
{
    int i;

    if (icon == NULL) {
        return;
    }
    for (i = 0; i < icon->npath; i++) {
        free(icon->path[i].pt);
    }
    free(icon->path);
    free(icon->style);
    free(icon->shape);
    free(icon);
}

int
tiku_hvif_style_count(const tiku_hvif_t *icon)
{
    return icon != NULL ? icon->nstyle : 0;
}

int
tiku_hvif_path_count(const tiku_hvif_t *icon)
{
    return icon != NULL ? icon->npath : 0;
}

int
tiku_hvif_shape_count(const tiku_hvif_t *icon)
{
    return icon != NULL ? icon->nshape : 0;
}

/* -------------------------------------------------------------- matrices */

/** @brief Compose so that @p a applies first, then @p b. */
static void
hvif_compose(const float *a, const float *b, float *out)
{
    float t[6];

    t[0] = a[0] * b[0] + a[1] * b[2];
    t[1] = a[0] * b[1] + a[1] * b[3];
    t[2] = a[2] * b[0] + a[3] * b[2];
    t[3] = a[2] * b[1] + a[3] * b[3];
    t[4] = a[4] * b[0] + a[5] * b[2] + b[4];
    t[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    memcpy(out, t, sizeof t);
}

/**
 * @brief Invert an affine.
 *
 * A collapsed matrix leaves @p out as the identity rather than untouched:
 * the gradient path reads it either way and then throws the result away.
 *
 * @return 0 on success, -1 if the matrix is degenerate.
 */
static int
hvif_invert(const float *m, float *out)
{
    float det = m[0] * m[3] - m[1] * m[2];

    if (fabsf(det) < 1e-12f) {
        memset(out, 0, 6 * sizeof *out);
        out[0] = out[3] = 1.0f;
        return -1;
    }
    out[0] =  m[3] / det;
    out[1] = -m[1] / det;
    out[2] = -m[2] / det;
    out[3] =  m[0] / det;
    out[4] = (m[2] * m[5] - m[3] * m[4]) / det;
    out[5] = (m[1] * m[4] - m[0] * m[5]) / det;
    return 0;
}

static void
hvif_xf(const float *m, float x, float y, float *ox, float *oy)
{
    *ox = x * m[0] + y * m[2] + m[4];
    *oy = x * m[1] + y * m[3] + m[5];
}

/* ------------------------------------------------------------ rasteriser */

typedef struct {
    float *acc;              /* (w + 2) * h signed-area deltas */
    float *cov;              /* w * h resolved coverage        */
    int    w, h, stride;
} hvif_raster_t;

/**
 * @brief Deposit one device-space edge into the accumulator.
 *
 * acc[i] holds the change in signed covered area between column i-1 and
 * column i; the trapezoid split is exact for a straight edge crossing a
 * pixel row.  Horizontal clamping to [0, w] preserves the crossing count,
 * which is what keeps each row's deposits summing to zero.
 */
static void
hvif_edge(hvif_raster_t *r, float x0, float y0, float x1, float y1)
{
    float dir = 1.0f, dxdy, x;
    int y, ylast;

    if (y0 == y1 || !(y0 == y0) || !(y1 == y1)) {
        return;                      /* horizontal, or NaN from bad input */
    }
    if (y0 > y1) {
        float t;

        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dir = -1.0f;
    }
    if (y1 <= 0.0f || y0 >= (float)r->h) {
        return;
    }
    dxdy = (x1 - x0) / (y1 - y0);
    if (y0 < 0.0f) {
        x0 -= y0 * dxdy;
        y0 = 0.0f;
    }
    if (y1 > (float)r->h) {
        y1 = (float)r->h;
    }
    x = x0;
    y = (int)floorf(y0);
    ylast = (int)ceilf(y1);
    for (; y < ylast; y++) {
        float top   = (y0 > (float)y) ? y0 : (float)y;
        float bot   = (y1 < (float)(y + 1)) ? y1 : (float)(y + 1);
        float dy    = bot - top;
        float xnext = x + dxdy * dy;
        float d     = dy * dir;
        float xa    = (x < xnext) ? x : xnext;
        float xb    = (x < xnext) ? xnext : x;
        float *row  = r->acc + (size_t)y * (size_t)r->stride;
        float x0f, x1f, s, a0, a1, a2, am, xm;
        int i0, i1, i;

        if (xa < 0.0f) { xa = 0.0f; }
        if (xb < 0.0f) { xb = 0.0f; }
        if (xa > (float)r->w) { xa = (float)r->w; }
        if (xb > (float)r->w) { xb = (float)r->w; }
        i0 = (int)floorf(xa);
        i1 = (int)ceilf(xb);
        if (i1 <= i0 + 1) {
            xm = 0.5f * (xa + xb) - (float)i0;
            row[i0]     += d * (1.0f - xm);
            row[i0 + 1] += d * xm;
        } else {
            s   = 1.0f / (xb - xa);
            x0f = xa - (float)i0;
            x1f = xb - (float)i1 + 1.0f;
            a0  = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
            am  = 0.5f * s * x1f * x1f;
            row[i0] += d * a0;
            if (i1 == i0 + 2) {
                row[i0 + 1] += d * (1.0f - a0 - am);
            } else {
                a1 = s * (1.5f - x0f);
                row[i0 + 1] += d * (a1 - a0);
                for (i = i0 + 2; i < i1 - 1; i++) {
                    row[i] += d * s;
                }
                a2 = a1 + (float)(i1 - i0 - 3) * s;
                row[i1 - 1] += d * (1.0f - a2 - am);
            }
            row[i1] += d * am;
        }
        x = xnext;
    }
}

static void
hvif_raster_begin(hvif_raster_t *r)
{
    memset(r->acc, 0, (size_t)r->stride * (size_t)r->h * sizeof *r->acc);
}

/**
 * @brief Resolve the accumulator to nonzero coverage in [0,1].
 *
 * The running sum restarts on every row.  Upstream font-rs runs one sum
 * over the whole buffer, which leaks a full-width streak into the next row
 * as soon as a shape is clipped at the right edge; per-row is identical for
 * well-formed input and immune to that.
 */
static void
hvif_raster_end(hvif_raster_t *r)
{
    int x, y;

    for (y = 0; y < r->h; y++) {
        const float *row = r->acc + (size_t)y * (size_t)r->stride;
        float *out = r->cov + (size_t)y * (size_t)r->w;
        float a = 0.0f;

        for (x = 0; x < r->w; x++) {
            float c;

            a += row[x];
            c = fabsf(a);                              /* NONZERO winding */
            out[x] = (c > 1.0f) ? 1.0f : c;
        }
    }
}

/* ------------------------------------------------------------- polylines */

typedef struct {
    float *v;                /* x,y pairs */
    int    n, cap;
} hvif_poly_t;

static int
hvif_poly_push(hvif_poly_t *p, float x, float y)
{
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 128;
        float *v = realloc(p->v, (size_t)cap * 2u * sizeof *v);

        if (v == NULL) {
            return -1;
        }
        p->v = v;
        p->cap = cap;
    }
    p->v[2 * p->n] = x;
    p->v[2 * p->n + 1] = y;
    p->n++;
    return 0;
}

/**
 * @brief Chords needed to hold a cubic within @p tol device pixels.
 *
 * Uniform subdivision into n chords deviates by at most 3M/(4n^2) with M
 * the larger second difference, so the count is closed form -- no
 * de Casteljau recursion and no depth cap to blow.
 */
static int
hvif_curve_steps(const float *c, float tol)
{
    float d1x = c[0] - 2.0f * c[2] + c[4], d1y = c[1] - 2.0f * c[3] + c[5];
    float d2x = c[2] - 2.0f * c[4] + c[6], d2y = c[3] - 2.0f * c[5] + c[7];
    float m1 = d1x * d1x + d1y * d1y;
    float m2 = d2x * d2x + d2y * d2y;
    float m = sqrtf((m1 > m2) ? m1 : m2);
    int n = (int)ceilf(sqrtf(0.75f * m / tol));

    if (n < 1) {
        n = 1;
    }
    if (n > 64) {
        n = 64;               /* guard pathological control points */
    }
    return n;
}

/** @brief Append samples 1..(n - @p drop) of a cubic in device space. */
static int
hvif_curve(hvif_poly_t *p, const float *c, int drop)
{
    int n = hvif_curve_steps(c, HVIF_TOL), k;

    for (k = 1; k <= n - drop; k++) {
        float t = (float)k / (float)n;
        float mt = 1.0f - t;
        float a = mt * mt * mt, b = 3.0f * mt * mt * t;
        float d = 3.0f * mt * t * t, e = t * t * t;

        if (hvif_poly_push(p, a * c[0] + b * c[2] + d * c[4] + e * c[6],
                              a * c[1] + b * c[3] + d * c[5] + e * c[7]) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Flatten one path into device space through @p m.
 *
 * Control points are transformed before flattening, so the tolerance is
 * measured in device pixels and a 16 px icon costs proportionally fewer
 * vertices than a 64 px one.  A path of fewer than two points contributes
 * nothing, exactly as get_path_storage() returns early.
 */
static int
hvif_flatten(const hvif_path_t *path, const float *m, hvif_poly_t *out)
{
    int i;

    out->n = 0;
    if (path->n < 2) {
        return 0;
    }
    {
        float x, y;

        hvif_xf(m, path->pt[0].x, path->pt[0].y, &x, &y);
        if (hvif_poly_push(out, x, y) != 0) {
            return -1;
        }
    }
    for (i = 1; i <= path->n; i++) {
        const hvif_pt_t *a = &path->pt[i - 1];
        const hvif_pt_t *b = &path->pt[i % path->n];
        float c[8];

        if (i == path->n && !path->closed) {
            break;
        }
        hvif_xf(m, a->x, a->y, &c[0], &c[1]);
        hvif_xf(m, a->ox, a->oy, &c[2], &c[3]);
        hvif_xf(m, b->ix, b->iy, &c[4], &c[5]);
        hvif_xf(m, b->x, b->y, &c[6], &c[7]);
        /* The wrap segment's last sample is the first vertex again; the
         * polyline closes itself, so drop it. */
        if (hvif_curve(out, c, (i == path->n) ? 1 : 0) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- filling */

/**
 * @brief Emit a closed subpath exactly as authored.
 *
 * The direction MUST survive: winding is the only signal that says an inner
 * subpath is a hole rather than more ink, and normalising it fills every
 * cutout solid.  Use hvif_emit_piece() for stroker geometry, which has the
 * opposite requirement.
 */
static void
hvif_emit_poly(hvif_raster_t *r, const float *v, int n)
{
    int i;

    if (n < 3) {
        return;
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;

        hvif_edge(r, v[2 * i], v[2 * i + 1], v[2 * j], v[2 * j + 1]);
    }
}

/**
 * @brief Emit one stroker piece, forced to a common winding direction.
 *
 * Quads and joint discs overlap constantly; opposite windings would cancel
 * to zero there and punch holes through the stroke.
 */
static void
hvif_emit_piece(hvif_raster_t *r, const float *v, int n)
{
    float area = 0.0f;
    int i;

    if (n < 3) {
        return;
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;

        area += v[2 * i] * v[2 * j + 1] - v[2 * j] * v[2 * i + 1];
    }
    if (area <= 0.0f) {
        hvif_emit_poly(r, v, n);
        return;
    }
    for (i = n - 1; i >= 0; i--) {
        int j = (i + n - 1) % n;

        hvif_edge(r, v[2 * i], v[2 * i + 1], v[2 * j], v[2 * j + 1]);
    }
}

/**
 * @brief Stroke a polyline by unioning quads and joint discs.
 *
 * Nonzero fill does the union for free, and the fabsf() clamp means the
 * overlaps leave no seam -- the same property that costs a hairline where
 * two separate shapes abut works in favour here.  Every piece is wound the
 * same way, or the overlaps would cancel into holes.
 */
static void
hvif_stroke(hvif_raster_t *r, const float *v, int n, int closed, float hw,
            int cap)
{
    float disc[2 * 32];
    int segs = closed ? n : n - 1;
    int ndisc, i, k;

    if (n < 2 || hw <= 0.0f) {
        return;
    }
    ndisc = (int)ceilf((float)M_PI / sqrtf(2.0f * HVIF_TOL / hw));
    if (ndisc < 6) {
        ndisc = 6;
    }
    if (ndisc > 32) {
        ndisc = 32;
    }
    for (i = 0; i < segs; i++) {
        int j = (i + 1) % n;
        float ax = v[2 * i], ay = v[2 * i + 1];
        float bx = v[2 * j], by = v[2 * j + 1];
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy);
        float nx, ny, quad[8];

        if (len < 1e-6f) {
            continue;
        }
        dx /= len;
        dy /= len;
        if (!closed && cap == 1) {                        /* square caps */
            if (i == 0) {
                ax -= dx * hw;
                ay -= dy * hw;
            }
            if (i == segs - 1) {
                bx += dx * hw;
                by += dy * hw;
            }
        }
        nx = -dy * hw;
        ny =  dx * hw;
        quad[0] = ax + nx; quad[1] = ay + ny;
        quad[2] = bx + nx; quad[3] = by + ny;
        quad[4] = bx - nx; quad[5] = by - ny;
        quad[6] = ax - nx; quad[7] = ay - ny;
        hvif_emit_piece(r, quad, 4);
    }
    /* A disc at every joint, plus the ends when the cap is round.  Miter
     * and bevel joins both land here as round; at these widths it does not
     * read. */
    for (i = 0; i < n; i++) {
        int interior = closed || (i > 0 && i < n - 1);

        if (!interior && cap != 2) {
            continue;
        }
        for (k = 0; k < ndisc; k++) {
            float a = 2.0f * (float)M_PI * (float)k / (float)ndisc;

            disc[2 * k]     = v[2 * i] + hw * cosf(a);
            disc[2 * k + 1] = v[2 * i + 1] + hw * sinf(a);
        }
        hvif_emit_piece(r, disc, ndisc);
    }
}

/* ----------------------------------------------------------- compositing */

/** @brief Exact round(a*b/255); the naive >>8 darkens over stacked shapes. */
static uint32_t
hvif_mul255(uint32_t a, uint32_t b)
{
    uint32_t t = a * b + 128u;

    return (t + (t >> 8)) >> 8;
}

static void
hvif_blend(uint32_t *dst, const uint8_t *c, uint32_t cov8)
{
    uint32_t a = hvif_mul255(c[3], cov8);
    uint32_t ia, d, r, g, b, o;

    if (a == 0u) {
        return;
    }
    ia = 255u - a;
    d = *dst;
    r = hvif_mul255(c[0], a) + hvif_mul255((d >> 16) & 0xFFu, ia);
    g = hvif_mul255(c[1], a) + hvif_mul255((d >> 8) & 0xFFu, ia);
    b = hvif_mul255(c[2], a) + hvif_mul255(d & 0xFFu, ia);
    o = a + hvif_mul255((d >> 24) & 0xFFu, ia);
    *dst = (o << 24) | (r << 16) | (g << 8) | b;
}

/** @brief Map a gradient-space point to a ramp index, per agg's spans. */
static int
hvif_ramp_index(uint8_t kind, float gx, float gy)
{
    float d1 = 0.0f, d2 = HVIF_CANVAS, f;
    int idx;

    switch (kind) {
    case HVIF_G_LINEAR:
        f = gx;
        d1 = -HVIF_CANVAS;
        break;
    case HVIF_G_CIRCULAR:
        f = sqrtf(gx * gx + gy * gy);
        break;
    case HVIF_G_DIAMOND:
        f = fmaxf(fabsf(gx), fabsf(gy));
        break;
    case HVIF_G_CONIC:
        f = fabsf(atan2f(gy, gx)) * HVIF_CANVAS / (float)M_PI;
        break;
    case HVIF_G_XY:
        f = fabsf(gx) * fabsf(gy) / HVIF_CANVAS;
        break;
    case HVIF_G_SQRT_XY:
        f = sqrtf(fabsf(gx) * fabsf(gy));
        break;
    default:
        f = gx;
        d1 = -HVIF_CANVAS;
        break;
    }
    idx = (int)((f - d1) * 256.0f / (d2 - d1));
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 255) {
        idx = 255;
    }
    return idx;
}

static void
hvif_composite(uint32_t *bmp, const hvif_raster_t *r, const hvif_style_t *st,
               const float *m)
{
    int x, y;

    if (!st->gradient) {
        for (y = 0; y < r->h; y++) {
            for (x = 0; x < r->w; x++) {
                float c = r->cov[(size_t)y * (size_t)r->w + x];
                uint32_t cov8;

                if (c <= 0.0f) {
                    continue;
                }
                cov8 = (uint32_t)(c * 255.0f + 0.5f);
                hvif_blend(&bmp[(size_t)y * (size_t)r->w + x], st->solid,
                           cov8);
            }
        }
        return;
    }
    {
        float chain[6], inv[6];
        int degenerate;

        /* HVIF gradients always inherit the shape transform: the format has
         * no flag to clear it.  Gradient matrix first, then the shape's,
         * then the global scale -- then invert the lot for the lookup. */
        hvif_compose(st->matrix, m, chain);
        degenerate = (hvif_invert(chain, inv) != 0);
        for (y = 0; y < r->h; y++) {
            float gx, gy;

            hvif_xf(inv, 0.5f, (float)y + 0.5f, &gx, &gy);
            for (x = 0; x < r->w; x++) {
                float c = r->cov[(size_t)y * (size_t)r->w + x];

                if (c > 0.0f) {
                    int idx = degenerate ? 0 :
                              hvif_ramp_index(st->kind, gx, gy);

                    hvif_blend(&bmp[(size_t)y * (size_t)r->w + x],
                               st->ramp[idx],
                               (uint32_t)(c * 255.0f + 0.5f));
                }
                gx += inv[0];
                gy += inv[1];
            }
        }
    }
}

/* ---------------------------------------------------------------- render */

int
tiku_hvif_render(const tiku_hvif_t *icon, uint32_t *bmp, int w, int h)
{
    hvif_raster_t r;
    hvif_poly_t poly;
    float g[6], scale;
    int i, rc = -1;

    if (icon == NULL || bmp == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    memset(bmp, 0, (size_t)w * (size_t)h * sizeof *bmp);
    memset(&poly, 0, sizeof poly);
    r.w = w;
    r.h = h;
    r.stride = w + 2;
    r.acc = calloc((size_t)r.stride * (size_t)h, sizeof *r.acc);
    r.cov = calloc((size_t)w * (size_t)h, sizeof *r.cov);
    if (r.acc == NULL || r.cov == NULL) {
        goto done;
    }
    /* Fit the 64-unit design square uniformly and centre it, so an
     * off-square request letterboxes rather than stretching. */
    scale = (float)((w < h) ? w : h) / HVIF_CANVAS;
    g[0] = scale;
    g[1] = 0.0f;
    g[2] = 0.0f;
    g[3] = scale;
    g[4] = ((float)w - HVIF_CANVAS * scale) * 0.5f;
    g[5] = ((float)h - HVIF_CANVAS * scale) * 0.5f;

    for (i = 0; i < icon->nshape; i++) {
        const hvif_shape_t *sh = &icon->shape[i];
        float m[6], det, sscale;
        int k;

        if (!sh->ok || sh->style >= icon->nstyle) {
            continue;             /* unknown record, or a dangling style */
        }
        /* LOD is not an optimisation: the artwork ships alternate small
         * and large versions of the same detail, gated on scale.  Ignore
         * it and both stack. */
        if (sh->lod && !(scale >= sh->lod_min &&
                         (scale <= sh->lod_max || sh->lod_max >= 4.0f))) {
            continue;
        }
        hvif_compose(sh->matrix, g, m);
        det = m[0] * m[3] - m[1] * m[2];
        sscale = sqrtf(fabsf(det));
        hvif_raster_begin(&r);
        for (k = 0; k < sh->npath; k++) {
            const hvif_path_t *p;

            if (sh->path[k] >= icon->npath) {
                continue;
            }
            p = &icon->path[sh->path[k]];
            if (hvif_flatten(p, m, &poly) != 0) {
                goto done;
            }
            if (sh->stroke) {
                /* A stroke leaves an unflagged path genuinely open; a
                 * CLOSED one still wraps.  Widths of zero would vanish, so
                 * they draw as a one-unit hairline instead. */
                float wid = sh->stroke_w > 0.0f ? sh->stroke_w : 1.0f;

                hvif_stroke(&r, poly.v, poly.n, p->closed,
                            wid * sscale * 0.5f, sh->stroke_cap);
            } else {
                /* Every subpath of the shape lands in one accumulator and
                 * fills nonzero together: that is how the holes are cut. */
                hvif_emit_poly(&r, poly.v, poly.n);
            }
        }
        hvif_raster_end(&r);
        hvif_composite(bmp, &r, &icon->style[sh->style], m);
    }
    rc = 0;
done:
    free(poly.v);
    free(r.acc);
    free(r.cov);
    return rc;
}

void
tiku_hvif_blit(tiku_surface_t *s, int dx, int dy, const uint32_t *bmp,
                   int w, int h)
{
    int x, y, x0, y0, x1, y1;

    if (s == NULL || bmp == NULL) {
        return;
    }
    x0 = s->clip.x;
    y0 = s->clip.y;
    x1 = s->clip.x + s->clip.w;
    y1 = s->clip.y + s->clip.h;
    if (x0 < dx) { x0 = dx; }
    if (y0 < dy) { y0 = dy; }
    if (x1 > dx + w) { x1 = dx + w; }
    if (y1 > dy + h) { y1 = dy + h; }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            uint32_t src = bmp[(size_t)(y - dy) * (size_t)w + (x - dx)];
            uint32_t ia = 255u - ((src >> 24) & 0xFFu);
            tiku_rgb_t d = tiku_peek(s, x, y);

            if (ia == 255u) {
                continue;
            }
            /* The source is premultiplied, so there is no multiply on its
             * side; the surface has no alpha, so it counts as opaque. */
            tiku_pixel(s, x, y,
                (((src >> 16) & 0xFFu) + hvif_mul255((d >> 16) & 0xFFu, ia))
                    << 16 |
                (((src >> 8) & 0xFFu) + hvif_mul255((d >> 8) & 0xFFu, ia))
                    << 8 |
                ((src & 0xFFu) + hvif_mul255(d & 0xFFu, ia)));
        }
    }
}
