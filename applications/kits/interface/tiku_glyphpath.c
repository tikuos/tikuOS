/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_glyphpath.c - one outline, however it was described.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_glyphpath.h"

#include <stdlib.h>
#include <string.h>

/* A glyph is small.  These bound what a hostile file can ask us for. */
#define PATH_MAX_EDGES  20000
#define PATH_MAX_STEPS  64
#define PATH_MAX_SIZE   1024    /* pixels across; a glyph is not a poster */
/*
 * No coordinate past this is worth carrying.  A font states the size of
 * its own em, and one that claims a thousandth of a pixel drives every
 * coordinate into the billions -- where converting a float to an int is
 * undefined, and lands on INT_MIN wherever the machine does not happen
 * to saturate.  Every edge is squared up to this on the way in, so
 * nothing downstream has to be clever.
 */
#define PATH_COORD_MAX  100000.0f

typedef struct {
    float x0, y0, x1, y1;
} path_edge_t;

struct tiku_path {
    path_edge_t *edge;
    int          count;
    int          room;
    float        x, y;          /* where the pen is */
    float        sx, sy;        /* where this contour started */
    int          started;
    int          failed;
    float        minx, miny, maxx, maxy;
};

float
tiku_hint_x(const tiku_hint_t *hint, float x)
{
    return x * hint->scale;
}

float
tiku_hint_y(const tiku_hint_t *hint, float y)
{
    float device = y * hint->scale;
    float shift = 0.0f;
    int i;

    if (hint->zones > 0 && y > 0.0f) {
        /* Between the baseline and the first zone the nudge comes in
         * proportionally, so a stem does not shear where it meets the
         * line; above the last zone it is simply carried. */
        if (y >= hint->from[hint->zones - 1]) {
            shift = hint->shift[hint->zones - 1];
        } else {
            float low = 0.0f, low_shift = 0.0f;

            for (i = 0; i < hint->zones; i++) {
                if (y < hint->from[i]) {
                    float span = hint->from[i] - low;

                    shift = (span > 0.0f)
                        ? low_shift + (hint->shift[i] - low_shift) *
                          ((y - low) / span)
                        : hint->shift[i];
                    break;
                }
                low = hint->from[i];
                low_shift = hint->shift[i];
            }
        }
    }
    /* Downwards on the screen is upwards in the font. */
    return -(device + shift);
}

tiku_path_t *
tiku_path_new(void)
{
    tiku_path_t *path = calloc(1u, sizeof *path);

    if (path == NULL) {
        return NULL;
    }
    path->room = 256;
    path->edge = malloc((size_t)path->room * sizeof *path->edge);
    if (path->edge == NULL) {
        free(path);
        return NULL;
    }
    tiku_path_reset(path);
    return path;
}

void
tiku_path_free(tiku_path_t *path)
{
    if (path != NULL) {
        free(path->edge);
        free(path);
    }
}

void
tiku_path_reset(tiku_path_t *path)
{
    if (path == NULL) {
        return;
    }
    path->count = 0;
    path->x = path->y = path->sx = path->sy = 0.0f;
    path->started = 0;
    path->failed = 0;
    path->minx = path->miny = 1.0e9f;
    path->maxx = path->maxy = -1.0e9f;
}

int
tiku_path_failed(const tiku_path_t *path)
{
    return (path == NULL) || path->failed;
}

/** @brief @p v, brought back into the range an int can hold. */
static float
sane(float v)
{
    /* Written against the NOT of each test, so that a NaN -- which
     * compares false with everything -- comes back as zero rather than
     * through. */
    if (!(v > -PATH_COORD_MAX)) {
        return (v == v) ? -PATH_COORD_MAX : 0.0f;
    }
    if (!(v < PATH_COORD_MAX)) {
        return PATH_COORD_MAX;
    }
    return v;
}

/** @brief Take note of a point for the outline's extent. */
static void
saw(tiku_path_t *path, float x, float y)
{
    if (x < path->minx) { path->minx = x; }
    if (x > path->maxx) { path->maxx = x; }
    if (y < path->miny) { path->miny = y; }
    if (y > path->maxy) { path->maxy = y; }
}

/** @brief Lay one edge down, growing the run if there is room to. */
static void
edge(tiku_path_t *path, float x0, float y0, float x1, float y1)
{
    if (path->failed) {
        return;
    }
    x0 = sane(x0);
    y0 = sane(y0);
    x1 = sane(x1);
    y1 = sane(y1);
    if (y0 == y1) {
        return;                 /* a flat edge covers nothing */
    }
    if (path->count >= path->room) {
        path_edge_t *grown;
        int want = path->room * 2;

        if (want > PATH_MAX_EDGES) {
            want = PATH_MAX_EDGES;
        }
        if (path->count >= want) {
            path->failed = 1;   /* an outline this complicated is not one */
            return;
        }
        grown = realloc(path->edge, (size_t)want * sizeof *path->edge);
        if (grown == NULL) {
            path->failed = 1;
            return;
        }
        path->edge = grown;
        path->room = want;
    }
    path->edge[path->count].x0 = x0;
    path->edge[path->count].y0 = y0;
    path->edge[path->count].x1 = x1;
    path->edge[path->count].y1 = y1;
    path->count++;
    saw(path, x0, y0);
    saw(path, x1, y1);
}

void
tiku_path_move(tiku_path_t *path, float x, float y)
{
    if (path == NULL) {
        return;
    }
    /* A new contour leaves the last one shut: a font that forgot to say
     * so meant to, and an open contour floods the whole glyph. */
    tiku_path_close(path);
    path->x = path->sx = x;
    path->y = path->sy = y;
    path->started = 1;
}

void
tiku_path_line(tiku_path_t *path, float x, float y)
{
    if (path == NULL) {
        return;
    }
    edge(path, path->x, path->y, x, y);
    path->x = x;
    path->y = y;
}

/** @brief How many straight pieces a curve that long deserves. */
static int
steps_for(float length)
{
    /* One piece per two pixels of control polygon: below that the eye
     * cannot tell, above it the accumulator does the work for nothing. */
    int n = (int)(length * 0.5f) + 2;

    if (n > PATH_MAX_STEPS) {
        n = PATH_MAX_STEPS;
    }
    return n;
}

static float
span(float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;

    if (dx < 0.0f) { dx = -dx; }
    if (dy < 0.0f) { dy = -dy; }
    return dx + dy;             /* the cheap length: this only picks a count */
}

void
tiku_path_quad(tiku_path_t *path, float cx, float cy, float x,
                    float y)
{
    float x0, y0;
    int steps, i;

    if (path == NULL) {
        return;
    }
    x0 = path->x;
    y0 = path->y;
    steps = steps_for(span(x0, y0, cx, cy) + span(cx, cy, x, y));
    for (i = 1; i <= steps; i++) {
        float u = (float)i / (float)steps, v = 1.0f - u;
        float px = v * v * x0 + 2.0f * v * u * cx + u * u * x;
        float py = v * v * y0 + 2.0f * v * u * cy + u * u * y;

        edge(path, path->x, path->y, px, py);
        path->x = px;
        path->y = py;
    }
}

void
tiku_path_cubic(tiku_path_t *path, float ax, float ay, float bx,
                     float by, float x, float y)
{
    float x0, y0;
    int steps, i;

    if (path == NULL) {
        return;
    }
    x0 = path->x;
    y0 = path->y;
    steps = steps_for(span(x0, y0, ax, ay) + span(ax, ay, bx, by) +
                      span(bx, by, x, y));
    for (i = 1; i <= steps; i++) {
        float u = (float)i / (float)steps, v = 1.0f - u;
        float uu = u * u, vv = v * v;
        float px = vv * v * x0 + 3.0f * vv * u * ax + 3.0f * v * uu * bx +
                   uu * u * x;
        float py = vv * v * y0 + 3.0f * vv * u * ay + 3.0f * v * uu * by +
                   uu * u * y;

        edge(path, path->x, path->y, px, py);
        path->x = px;
        path->y = py;
    }
}

void
tiku_path_close(tiku_path_t *path)
{
    if (path == NULL || !path->started) {
        return;
    }
    if (path->x != path->sx || path->y != path->sy) {
        edge(path, path->x, path->y, path->sx, path->sy);
    }
    path->x = path->sx;
    path->y = path->sy;
    path->started = 0;
}

void
tiku_path_bounds(const tiku_path_t *path, int *x0, int *y0,
                      int *w, int *h)
{
    int ax, ay, bx, by;

    if (path == NULL || path->count == 0) {
        if (x0 != NULL) { *x0 = 0; }
        if (y0 != NULL) { *y0 = 0; }
        if (w != NULL) { *w = 0; }
        if (h != NULL) { *h = 0; }
        return;
    }
    ax = (int)((path->minx < 0.0f) ? path->minx - 1.0f : path->minx) - 1;
    ay = (int)((path->miny < 0.0f) ? path->miny - 1.0f : path->miny) - 1;
    bx = (int)(path->maxx + 0.9999f) + 1;
    by = (int)(path->maxy + 0.9999f) + 1;
    if (x0 != NULL) { *x0 = ax; }
    if (y0 != NULL) { *y0 = ay; }
    if (w != NULL) { *w = (bx - ax > PATH_MAX_SIZE) ? PATH_MAX_SIZE
                                                    : bx - ax; }
    if (h != NULL) { *h = (by - ay > PATH_MAX_SIZE) ? PATH_MAX_SIZE
                                                    : by - ay; }
}

/**
 * @brief Accumulate one edge's signed area into @p a.
 *
 * Row-major with a two-cell margin, because an edge writes into the cell
 * after the one it covers and the last of those must not land in the
 * next row.
 */
static void
accumulate(float *a, int w, int h, float x0, float y0, float x1, float y1)
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
    if (y0 < 0.0f) {
        x -= y0 * dxdy;
        y = 0;
    } else {
        y = (int)((y0 > (float)h) ? (float)h : y0);
    }
    ylast = (int)((y1 > (float)h) ? (float)h : y1 + 0.9999f);
    if (ylast > h) {
        ylast = h;
    }
    for (; y < ylast; y++) {
        float top = ((float)y > y0) ? (float)y : y0;
        float bot = ((float)(y + 1) < y1) ? (float)(y + 1) : y1;
        float dy = bot - top;
        float xnext = x + dxdy * dy;
        float d = dy * dir;
        float xa = (x < xnext) ? x : xnext;
        float xb = (x < xnext) ? xnext : x;
        float xaf;
        int xai, xbi;
        float *row = a + (size_t)y * (size_t)(w + 2);

        /* Into the row before the casts, not after: past INT_MAX the
         * conversion has no defined answer to clamp. */
        if (!(xa > 0.0f)) { xa = 0.0f; }
        if (!(xb > 0.0f)) { xb = 0.0f; }
        if (xa > (float)w) { xa = (float)w; }
        if (xb > (float)w) { xb = (float)w; }
        xaf = (float)(int)xa;
        xai = (int)xaf;
        xbi = (int)(xb + 0.9999f);
        if (xai >= w) {
            x = xnext;
            continue;           /* off the right: it covers nothing here */
        }
        if (xbi > w) {
            xbi = w;
        }
        if (xbi <= xai + 1) {
            float mid = 0.5f * (x + xnext) - xaf;

            if (mid < 0.0f) { mid = 0.0f; }
            if (mid > 1.0f) { mid = 1.0f; }
            row[xai] += d - d * mid;
            row[xai + 1] += d * mid;
        } else {
            /* Crossing cells: the first and last get their corner
             * triangles, the ones between a full slice each. */
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

unsigned char *
tiku_path_render(const tiku_path_t *path, int x0, int y0, int w,
                      int h, int darken)
{
    unsigned char *out;
    float *a;
    int i, x, y;

    if (path == NULL || w <= 0 || h <= 0 ||
        w > PATH_MAX_SIZE || h > PATH_MAX_SIZE) {
        return NULL;
    }
    a = calloc((size_t)(w + 2) * (size_t)h, sizeof *a);
    out = calloc((size_t)w * (size_t)h, 1u);
    if (a == NULL || out == NULL) {
        free(a);
        free(out);
        return NULL;
    }
    for (i = 0; i < path->count; i++) {
        accumulate(a, w, h,
                   path->edge[i].x0 - (float)x0, path->edge[i].y0 - (float)y0,
                   path->edge[i].x1 - (float)x0, path->edge[i].y1 - (float)y0);
    }
    for (y = 0; y < h; y++) {
        const float *row = a + (size_t)y * (size_t)(w + 2);
        float acc = 0.0f;

        for (x = 0; x < w; x++) {
            float cov;
            int v;

            acc += row[x];
            cov = (acc < 0.0f) ? -acc : acc;
            if (cov > 1.0f) {
                cov = 1.0f;
            }
            /*
             * Stem darkening.  Analytic coverage is honest -- a stem half
             * a pixel wide is drawn half grey -- but honest is washed out
             * at ten and twelve pixels, where the baked faces (rendered
             * by a hinting library) put down a solid stem.  a gentle
             * lift, c(1 + 0.4(1-c)), brings partial coverage toward ink
             * the way that library's gamma
             * does, in one multiply and no table, and leaves nothing
             * fully covered or fully clear where it was.
             */
            if (darken) {
                cov = cov * (1.0f + 0.4f * (1.0f - cov));
            }
            v = (int)(cov * 255.0f + 0.5f);
            out[(size_t)y * (size_t)w + x] = (unsigned char)((v > 255) ? 255
                                                                      : v);
        }
    }
    free(a);
    return out;
}
