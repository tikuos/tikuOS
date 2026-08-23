/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_logo.c - the TikuOS mark.
 *
 * The artwork is strokes and the rasteriser fills, so every line here is
 * turned into the quadrilateral it covers: a segment becomes a rectangle
 * of the stroke's width laid along it, extended half a width past each
 * end so the corners meet.  Where two of those overlap at a joint the
 * accumulator adds them and clamps, which is exactly the union wanted --
 * so the joins cost nothing but the overlap.
 *
 * Colours are drawn back to front in the order the artwork has them,
 * each as ONE path of many quads: six coverage maps for the whole mark
 * rather than one per line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <stdlib.h>

#include "tiku_glyphpath.h"
#include "tiku_logo.h"

/* The artwork's own space.  Every number below is read straight off it. */
#define U      1024.0f
#define STROKE   23.34f

/*
 * The sets.  The first is the artwork as drawn and is what the mark IS;
 * the others keep its structure -- a light ground, near-black near edges,
 * a muted far cube, one saturated accent on the lit edges and a pale
 * version of it on the face -- and change only the hue.  Structure is
 * what makes a mark recognisable at a glance; hue is what makes it worth
 * looking at twice.
 *
 * The ground travels with the set rather than following the theme: a
 * logo whose colours are chosen by the furniture around it is not doing
 * a logo's job.
 */
static const tiku_logo_palette_t palettes[] = {
    {   /* as drawn */
        TIKU_RGB(0xF2, 0xF1, 0xEC), TIKU_RGB(0x22, 0x25, 0x2A),
        TIKU_RGB(0x5C, 0x66, 0x72), TIKU_RGB(0xFF, 0xC7, 0x00),
        TIKU_RGB(0xF7, 0xE6, 0x89), TIKU_RGB(0xCE, 0xB1, 0x09),
        TIKU_RGB(0xCE, 0xB6, 0x22)
    },
    {   /* the Be blue the selection has always been */
        TIKU_RGB(0xEC, 0xF0, 0xF4), TIKU_RGB(0x1B, 0x27, 0x33),
        TIKU_RGB(0x5A, 0x71, 0x84), TIKU_RGB(0x2E, 0x86, 0xDE),
        TIKU_RGB(0xBF, 0xDC, 0xF7), TIKU_RGB(0x2C, 0x6E, 0xA8),
        TIKU_RGB(0x3A, 0x7F, 0xB8)
    },
    {   /* rose */
        TIKU_RGB(0xF5, 0xEE, 0xEE), TIKU_RGB(0x2A, 0x22, 0x24),
        TIKU_RGB(0x7A, 0x5C, 0x63), TIKU_RGB(0xE0, 0x48, 0x5F),
        TIKU_RGB(0xF7, 0xC9, 0xD0), TIKU_RGB(0xB0, 0x3A, 0x4C),
        TIKU_RGB(0xC0, 0x44, 0x56)
    },
    {   /* moss */
        TIKU_RGB(0xED, 0xF2, 0xEC), TIKU_RGB(0x1F, 0x2A, 0x20),
        TIKU_RGB(0x5E, 0x73, 0x60), TIKU_RGB(0x4C, 0xAF, 0x50),
        TIKU_RGB(0xC8, 0xE6, 0xC9), TIKU_RGB(0x3E, 0x8E, 0x41),
        TIKU_RGB(0x46, 0xA0, 0x4A)
    }
};

#define PALETTE_COUNT ((int)(sizeof palettes / sizeof palettes[0]))

/* A run of points; a closed run joins its last back to its first. */
typedef struct {
    int          n;
    int          closed;
    const float *pt;                /* n pairs, in artwork units */
} run_t;

/* The near cube: the one that survives on its own when there is no room
 * for both. */
static const float near_hex[] = {
    385.67f, 118.15f, 638.33f, 264.02f, 638.33f, 555.76f,
    385.67f, 701.63f, 133.02f, 555.76f, 133.02f, 264.02f
};
static const float near_spine[] = { 385.67f, 118.15f, 385.67f, 701.63f };
static const float near_diag1[] = { 133.02f, 264.02f, 638.33f, 555.76f };
static const float near_diag2[] = { 638.33f, 264.02f, 133.02f, 555.76f };

static const float far_hex[] = {
    638.33f, 322.37f, 890.98f, 468.24f, 890.98f, 759.98f,
    638.33f, 905.85f, 385.67f, 759.98f, 385.67f, 468.24f
};
static const float far_spine[] = { 638.33f, 322.37f, 638.33f, 905.85f };
static const float far_diag1[] = { 385.67f, 468.24f, 890.98f, 759.98f };
static const float far_diag2[] = { 890.98f, 468.24f, 385.67f, 759.98f };

/* The lit edges: where the two cubes meet, and the amber is the whole
 * reason the mark reads as two solids rather than a tangle of lines. */
static const float lit_near[] = { 385.67f, 701.63f, 638.33f, 555.76f };
static const float lit_far[]  = { 385.67f, 468.24f, 638.33f, 322.37f };
static const float mix_a[]    = { 436.20f, 439.07f, 638.33f, 555.76f };
static const float mix_b[]    = { 385.67f, 468.24f, 587.80f, 584.93f };

/* The one filled face. */
static const float face[] = {
    385.67f, 468.24f, 638.33f, 322.37f, 638.33f, 555.76f, 385.67f, 701.63f
};

/** @brief Where the artwork sits on the surface, and how big. */
typedef struct {
    float x, y, side;
} fit_t;

static float
at_x(const fit_t *f, float u)
{
    return f->x + u * f->side / U;
}

static float
at_y(const fit_t *f, float u)
{
    return f->y + u * f->side / U;
}

/**
 * @brief Add the quadrilateral a stroked segment covers.
 *
 * Extended half a width past each end, which squares the cap and fills
 * the joint at the same time: the next segment's own extension overlaps
 * it, and overlapping coverage is added and clamped rather than cancelled.
 */
static void
segment(tiku_path_t *p, const fit_t *f, float ax, float ay, float bx,
        float by, float w)
{
    float x0 = at_x(f, ax), y0 = at_y(f, ay);
    float x1 = at_x(f, bx), y1 = at_y(f, by);
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float ux, uy, nx, ny, half = w * 0.5f;

    if (len < 0.0001f) {
        return;
    }
    ux = dx / len;
    uy = dy / len;
    nx = -uy * half;
    ny = ux * half;
    x0 -= ux * half;
    y0 -= uy * half;
    x1 += ux * half;
    y1 += uy * half;
    tiku_path_move(p, x0 + nx, y0 + ny);
    tiku_path_line(p, x1 + nx, y1 + ny);
    tiku_path_line(p, x1 - nx, y1 - ny);
    tiku_path_line(p, x0 - nx, y0 - ny);
    tiku_path_close(p);
}

/** @brief Every segment of @p r, stroked. */
static void
stroke_run(tiku_path_t *p, const fit_t *f, const run_t *r, float w)
{
    int i;

    for (i = 0; i + 1 < r->n; i++) {
        segment(p, f, r->pt[i * 2], r->pt[i * 2 + 1],
                r->pt[i * 2 + 2], r->pt[i * 2 + 3], w);
    }
    if (r->closed && r->n > 2) {
        segment(p, f, r->pt[(r->n - 1) * 2], r->pt[(r->n - 1) * 2 + 1],
                r->pt[0], r->pt[1], w);
    }
}

/** @brief Lay the coverage @p cov down in @p c, blended by its own alpha. */
static void
ink(tiku_surface_t *s, const unsigned char *cov, int x0, int y0, int w,
    int h, tiku_rgb_t c)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned a = cov[(size_t)y * (size_t)w + x];
            tiku_rgb_t under;
            unsigned r, g, b;

            if (a == 0u) {
                continue;
            }
            under = tiku_peek(s, x0 + x, y0 + y);
            r = (((c >> 16) & 0xFFu) * a +
                 ((under >> 16) & 0xFFu) * (255u - a)) / 255u;
            g = (((c >> 8) & 0xFFu) * a +
                 ((under >> 8) & 0xFFu) * (255u - a)) / 255u;
            b = ((c & 0xFFu) * a + (under & 0xFFu) * (255u - a)) / 255u;
            tiku_pixel(s, x0 + x, y0 + y, TIKU_RGB(r, g, b));
        }
    }
}

/** @brief Build, render and lay down one colour's worth of the mark. */
static void
draw_path(tiku_surface_t *s, tiku_path_t *p, tiku_rgb_t c, int mono,
          float shade)
{
    int x0, y0, w, h;
    unsigned char *cov;

    if (tiku_path_failed(p)) {
        return;
    }
    tiku_path_bounds(p, &x0, &y0, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    cov = tiku_path_render(p, x0, y0, w, h, 0);
    if (cov == NULL) {
        return;
    }
    /*
     * The two runtime transforms, in the one place every colour passes
     * through: the shade the caller asked for, and the drain a palette
     * without hue asks for on its behalf.  Shade first, so lightening
     * the mark for a dark button and draining it for a grey desktop
     * compose rather than fight.
     */
    if (shade != 1.0f) {
        c = tiku_tint(c, shade);
    }
    ink(s, cov, x0, y0, w, h, mono ? tiku_grey(c) : c);
    free(cov);
}

/** @brief One run in one colour, from a fresh path. */
static void
lay(tiku_surface_t *s, const fit_t *f, const float *pt, int n, int closed,
    float w, tiku_rgb_t c, int mono, float shade)
{
    tiku_path_t *p = tiku_path_new();
    run_t r;

    if (p == NULL) {
        return;
    }
    r.n = n;
    r.closed = closed;
    r.pt = pt;
    stroke_run(p, f, &r, w);
    draw_path(s, p, c, mono, shade);
    tiku_path_free(p);
}

int
tiku_logo_palette_count(void)
{
    return PALETTE_COUNT;
}

/** @brief @p a and @p b mixed, @p t of the way from one to the other. */
static tiku_rgb_t
mix(tiku_rgb_t a, tiku_rgb_t b, float t)
{
    unsigned i, out[3];

    for (i = 0; i < 3u; i++) {
        int sh = 16 - 8 * (int)i;
        float x = (float)((a >> sh) & 0xFFu);
        float y = (float)((b >> sh) & 0xFFu);
        float v = x + (y - x) * t;

        out[i] = (unsigned)(v + 0.5f);
        if (out[i] > 255u) {
            out[i] = 255u;
        }
    }
    return TIKU_RGB(out[0], out[1], out[2]);
}

void
tiku_logo_palette(int i, float toward_next, tiku_logo_palette_t *out)
{
    const tiku_logo_palette_t *a, *b;

    if (out == NULL) {
        return;
    }
    if (toward_next < 0.0f) { toward_next = 0.0f; }
    if (toward_next > 1.0f) { toward_next = 1.0f; }
    i = ((i % PALETTE_COUNT) + PALETTE_COUNT) % PALETTE_COUNT;
    a = &palettes[i];
    b = &palettes[(i + 1) % PALETTE_COUNT];

    out->ground = mix(a->ground, b->ground, toward_next);
    out->dark   = mix(a->dark,   b->dark,   toward_next);
    out->grey   = mix(a->grey,   b->grey,   toward_next);
    out->accent = mix(a->accent, b->accent, toward_next);
    out->tint   = mix(a->tint,   b->tint,   toward_next);
    out->mix_a  = mix(a->mix_a,  b->mix_a,  toward_next);
    out->mix_b  = mix(a->mix_b,  b->mix_b,  toward_next);
}

tiku_rgb_t
tiku_logo_ground(void)
{
    tiku_rgb_t g = palettes[0].ground;

    return tiku_theme_achromatic() ? tiku_grey(g) : g;
}

/** @brief Fill a closed run, for the faces that are solid. */
static void
fill_run(tiku_surface_t *s, const fit_t *f, const float *pt, int n,
         tiku_rgb_t c, int mono, float shade)
{
    tiku_path_t *p = tiku_path_new();
    int i;

    if (p == NULL) {
        return;
    }
    tiku_path_move(p, at_x(f, pt[0]), at_y(f, pt[1]));
    for (i = 1; i < n; i++) {
        tiku_path_line(p, at_x(f, pt[i * 2]), at_y(f, pt[i * 2 + 1]));
    }
    tiku_path_close(p);
    draw_path(s, p, c, mono, shade);
    tiku_path_free(p);
}

void
tiku_logo_paint_with(tiku_surface_t *s, tiku_rect_t r, unsigned flags,
                     const tiku_logo_palette_t *p, float shade)
{
    int mono = tiku_theme_achromatic();
    int side = (r.w < r.h) ? r.w : r.h;
    tiku_logo_palette_t use;
    fit_t f;
    float w;
    int both, solid;

    if (s == NULL || side < 4) {
        return;
    }
    if (p != NULL) {
        use = *p;
    } else {
        tiku_logo_palette(0, 0.0f, &use);
    }
    f.side = (float)side;
    f.x = (float)r.x + (float)(r.w - side) * 0.5f;
    f.y = (float)r.y + (float)(r.h - side) * 0.5f;

    if ((flags & TIKU_LOGO_GROUND) != 0u) {
        tiku_rect_t ground;
        tiku_rgb_t g = use.ground;

        if (shade != 1.0f) {
            g = tiku_tint(g, shade);
        }
        ground.x = (int)f.x;
        ground.y = (int)f.y;
        ground.w = side;
        ground.h = side;
        tiku_fill(s, ground, mono ? tiku_grey(g) : g);
    }

    /*
     * The stroke as drawn, but never thinner than a pixel: below that a
     * line is not a faint line, it is a line that is missing in places,
     * and the mark comes apart into dashes.
     */
    w = STROKE * (float)side / U;
    if (w < 1.0f) {
        w = 1.0f;
    }
    both = (side >= 32);
    solid = (side < 22);

    /*
     * Small, the near cube is filled before it is drawn: at sixteen
     * pixels an outline is four grey lines and a hole, and a solid with
     * one lit face is still recognisably this mark.
     */
    if (solid) {
        fill_run(s, &f, near_hex, 6, use.tint, mono, shade);
    }
    fill_run(s, &f, face, 4, use.tint, mono, shade);
    lay(s, &f, near_hex, 6, 1, w, use.dark, mono, shade);
    lay(s, &f, near_spine, 2, 0, w, use.dark, mono, shade);
    if (!solid) {
        /* The two diagonals are what make a wireframe read as a cube.
         * Over the filled small rendition they only crowd it: the
         * silhouette and the lit face are already doing that work. */
        lay(s, &f, near_diag1, 2, 0, w, use.dark, mono, shade);
        lay(s, &f, near_diag2, 2, 0, w, use.dark, mono, shade);
    }
    lay(s, &f, lit_near, 2, 0, w, use.accent, mono, shade);
    lay(s, &f, mix_a, 2, 0, w, use.mix_a, mono, shade);

    if (both) {
        lay(s, &f, far_hex, 6, 1, w, use.grey, mono, shade);
        lay(s, &f, far_spine, 2, 0, w, use.grey, mono, shade);
        lay(s, &f, far_diag1, 2, 0, w, use.grey, mono, shade);
        lay(s, &f, far_diag2, 2, 0, w, use.grey, mono, shade);
        lay(s, &f, lit_far, 2, 0, w, use.accent, mono, shade);
        lay(s, &f, mix_b, 2, 0, w, use.mix_b, mono, shade);
    }
}

void
tiku_logo_paint(tiku_surface_t *s, tiku_rect_t r, unsigned flags)
{
    tiku_logo_paint_with(s, r, flags, NULL, 1.0f);
}
