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
 * The palette, named as the artwork names it.  The ground is part of the
 * mark and not part of the theme: it is the same off-white on a dark
 * desktop as on a light one, because a logo that changes colour with the
 * furniture is not doing a logo's job.
 */
#define C_GROUND  TIKU_RGB(0xF2, 0xF1, 0xEC)
#define C_DARK    TIKU_RGB(0x22, 0x25, 0x2A)
#define C_GREY    TIKU_RGB(0x5C, 0x66, 0x72)
#define C_ACCENT  TIKU_RGB(0xFF, 0xC7, 0x00)
#define C_TINT    TIKU_RGB(0xF7, 0xE6, 0x89)
#define C_MIX_A   TIKU_RGB(0xCE, 0xB1, 0x09)
#define C_MIX_B   TIKU_RGB(0xCE, 0xB6, 0x22)

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
draw_path(tiku_surface_t *s, tiku_path_t *p, tiku_rgb_t c, int mono)
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
    ink(s, cov, x0, y0, w, h, mono ? tiku_grey(c) : c);
    free(cov);
}

/** @brief One run in one colour, from a fresh path. */
static void
lay(tiku_surface_t *s, const fit_t *f, const float *pt, int n, int closed,
    float w, tiku_rgb_t c, int mono)
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
    draw_path(s, p, c, mono);
    tiku_path_free(p);
}

tiku_rgb_t
tiku_logo_ground(void)
{
    return tiku_theme_achromatic() ? tiku_grey(C_GROUND) : C_GROUND;
}

/** @brief Fill a closed run, for the faces that are solid. */
static void
fill_run(tiku_surface_t *s, const fit_t *f, const float *pt, int n,
         tiku_rgb_t c, int mono)
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
    draw_path(s, p, c, mono);
    tiku_path_free(p);
}

void
tiku_logo_paint(tiku_surface_t *s, tiku_rect_t r, unsigned flags)
{
    int mono = tiku_theme_achromatic();
    int side = (r.w < r.h) ? r.w : r.h;
    fit_t f;
    float w;
    tiku_rect_t ground;
    int both, solid;

    if (s == NULL || side < 4) {
        return;
    }
    f.side = (float)side;
    f.x = (float)r.x + (float)(r.w - side) * 0.5f;
    f.y = (float)r.y + (float)(r.h - side) * 0.5f;

    if ((flags & TIKU_LOGO_GROUND) != 0u) {
        ground.x = (int)f.x;
        ground.y = (int)f.y;
        ground.w = side;
        ground.h = side;
        tiku_fill(s, ground, tiku_logo_ground());
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
        fill_run(s, &f, near_hex, 6, C_TINT, mono);
    }
    fill_run(s, &f, face, 4, C_TINT, mono);
    lay(s, &f, near_hex, 6, 1, w, C_DARK, mono);
    lay(s, &f, near_spine, 2, 0, w, C_DARK, mono);
    if (!solid) {
        /* The two diagonals are what make a wireframe read as a cube.
         * Over the filled small rendition they only crowd it: the
         * silhouette and the lit face are already doing that work. */
        lay(s, &f, near_diag1, 2, 0, w, C_DARK, mono);
        lay(s, &f, near_diag2, 2, 0, w, C_DARK, mono);
    }
    lay(s, &f, lit_near, 2, 0, w, C_ACCENT, mono);
    lay(s, &f, mix_a, 2, 0, w, C_MIX_A, mono);

    if (both) {
        lay(s, &f, far_hex, 6, 1, w, C_GREY, mono);
        lay(s, &f, far_spine, 2, 0, w, C_GREY, mono);
        lay(s, &f, far_diag1, 2, 0, w, C_GREY, mono);
        lay(s, &f, far_diag2, 2, 0, w, C_GREY, mono);
        lay(s, &f, lit_far, 2, 0, w, C_ACCENT, mono);
        lay(s, &f, mix_b, 2, 0, w, C_MIX_B, mono);
    }
}
