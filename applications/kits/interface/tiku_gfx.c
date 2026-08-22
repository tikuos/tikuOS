/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gfx.c - surface primitives and the PNG writer.
 *
 * Every primitive clips against the surface's rectangle, so callers draw in
 * widget coordinates and never test bounds themselves.  The PNG writer emits
 * stored (uncompressed) deflate blocks: a few lines of code instead of a zlib
 * dependency, and screenshots are not a size-critical path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_scale_pixels(tiku_rgb_t *destination,
                       int destination_width, int destination_height,
                       const tiku_rgb_t *source,
                       int source_width, int source_height)
{
    int scale, x, y, repeat;

    if (destination == NULL || source == NULL || destination_width <= 0 ||
        destination_height <= 0 || source_width <= 0 || source_height <= 0) {
        return;
    }
    scale = destination_width / source_width;
    if (scale > 1 && destination_width == source_width * scale &&
        destination_height == source_height * scale) {
        for (y = 0; y < source_height; y++) {
            tiku_rgb_t *row = destination +
                (size_t)y * scale * destination_width;

            for (x = 0; x < source_width; x++) {
                for (repeat = 0; repeat < scale; repeat++) {
                    row[x * scale + repeat] =
                        source[(size_t)y * source_width + x];
                }
            }
            for (repeat = 1; repeat < scale; repeat++) {
                memcpy(row + (size_t)repeat * destination_width, row,
                       (size_t)destination_width * sizeof *row);
            }
        }
        return;
    }
    if (destination_width == source_width &&
        destination_height == source_height) {
        memcpy(destination, source,
               (size_t)source_width * source_height * sizeof *source);
        return;
    }
    for (y = 0; y < destination_height; y++) {
        int source_y = (int)((int64_t)y * source_height /
                             destination_height);

        for (x = 0; x < destination_width; x++) {
            int source_x = (int)((int64_t)x * source_width /
                                 destination_width);

            destination[(size_t)y * destination_width + x] =
                source[(size_t)source_y * source_width + source_x];
        }
    }
}

/** @brief Native pixels per logical pixel; a zeroed surface reads as 1. */
static int
scale_of(const tiku_surface_t *s)
{
    return (s != NULL && s->scale > 1) ? s->scale : 1;
}

tiku_surface_t *
tiku_surface_new(int w, int h, tiku_rgb_t bg)
{
    tiku_surface_t *s;
    long i, n = (long)w * (long)h;   /* scale 1: native is logical */

    if (w <= 0 || h <= 0) {
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->px = malloc((size_t)n * sizeof *s->px);
    if (s->px == NULL) {
        free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    for (i = 0; i < n; i++) {
        s->px[i] = bg;
    }
    tiku_clip_reset(s);
    return s;
}

int
tiku_surface_resize(tiku_surface_t *s, int w, int h,
                         tiku_rgb_t bg)
{
    tiku_rgb_t *pixels;
    size_t i, count, sc;

    if (s == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    sc = (size_t)scale_of(s);
    if ((size_t)w * sc > SIZE_MAX / sizeof *pixels / ((size_t)h * sc)) {
        return -1;
    }
    count = (size_t)w * sc * (size_t)h * sc;
    pixels = malloc(count * sizeof *pixels);
    if (pixels == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        pixels[i] = bg;
    }
    free(s->px);
    s->px = pixels;
    s->w = w;
    s->h = h;
    tiku_clip_reset(s);
    return 0;
}

int
tiku_surface_rescale(tiku_surface_t *s, int scale,
                          tiku_rgb_t bg)
{
    if (s == NULL || scale < 1 || scale > 4) {
        return -1;
    }
    s->scale = scale;
    return tiku_surface_resize(s, s->w, s->h, bg);
}

tiku_rgb_t
tiku_peek(const tiku_surface_t *s, int x, int y)
{
    int sc = scale_of(s);

    if (s == NULL || s->px == NULL || x < 0 || y < 0 ||
        x >= s->w || y >= s->h) {
        return 0;
    }
    return s->px[(long)y * sc * ((long)s->w * sc) + (long)x * sc];
}

void
tiku_surface_free(tiku_surface_t *s)
{
    if (s != NULL) {
        free(s->px);
        free(s);
    }
}

void
tiku_clip_reset(tiku_surface_t *s)
{
    if (s != NULL) {
        s->clip.x = 0;
        s->clip.y = 0;
        s->clip.w = s->w;
        s->clip.h = s->h;
    }
}

void
tiku_clip_set(tiku_surface_t *s, tiku_rect_t r)
{
    int x0, y0, x1, y1;

    if (s == NULL) {
        return;
    }
    x0 = (r.x > 0) ? r.x : 0;
    y0 = (r.y > 0) ? r.y : 0;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    s->clip.x = x0;
    s->clip.y = y0;
    s->clip.w = (x1 > x0) ? x1 - x0 : 0;
    s->clip.h = (y1 > y0) ? y1 - y0 : 0;
}

tiku_rgb_t
tiku_tint(tiku_rgb_t c, float tint)
{
    int i, out[3];
    int ch[3];

    ch[0] = (int)((c >> 16) & 0xFFu);
    ch[1] = (int)((c >> 8) & 0xFFu);
    ch[2] = (int)(c & 0xFFu);
    for (i = 0; i < 3; i++) {
        float v;
        if (tint < 1.0f) {
            v = 255.0f - (255.0f - (float)ch[i]) * tint;
        } else {
            v = (float)ch[i] * (2.0f - tint);
        }
        if (v < 0.0f)   { v = 0.0f; }
        if (v > 255.0f) { v = 255.0f; }
        out[i] = (int)(v + 0.5f);
    }
    return TIKU_RGB(out[0], out[1], out[2]);
}

void
tiku_pixel(tiku_surface_t *s, int x, int y, tiku_rgb_t c)
{
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = c;
        }
    }
}

void
tiku_blit(tiku_surface_t *dst, int x, int y,
               const tiku_surface_t *src)
{
    int y0, y1, x0, x1, row;

    if (dst == NULL || src == NULL || dst->px == NULL || src->px == NULL) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + src->w;
    y1 = y + src->h;
    /* The clip alone: it is held inside the surface, so trimming to it
     * trims to the surface too. */
    if (x0 < dst->clip.x)               { x0 = dst->clip.x; }
    if (y0 < dst->clip.y)               { y0 = dst->clip.y; }
    if (x1 > dst->clip.x + dst->clip.w) { x1 = dst->clip.x + dst->clip.w; }
    if (y1 > dst->clip.y + dst->clip.h) { y1 = dst->clip.y + dst->clip.h; }
    if (scale_of(dst) == scale_of(src)) {
        int sc = scale_of(dst), sub;
        long dstride = (long)dst->w * sc;
        long sstride = (long)src->w * sc;

        for (row = y0; row < y1; row++) {
            for (sub = 0; sub < sc; sub++) {
                memcpy(dst->px + ((long)row * sc + sub) * dstride +
                       (long)x0 * sc,
                       src->px + ((long)(row - y) * sc + sub) * sstride +
                       (long)(x0 - x) * sc,
                       (size_t)(x1 - x0) * sc * sizeof *dst->px);
            }
        }
    } else {
        /* Unequal scales: carry each source pixel over logically. */
        int col;
        int ss = scale_of(src);
        long sstride = (long)src->w * ss;

        for (row = y0; row < y1; row++) {
            for (col = x0; col < x1; col++) {
                tiku_pixel(dst, col, row,
                    src->px[(long)(row - y) * ss * sstride +
                            (long)(col - x) * ss]);
            }
        }
    }
}

void
tiku_copy_bits(tiku_surface_t *s, tiku_rect_t src, int dx,
                    int dy)
{
    int y, x0, y0, x1, y1, w;

    if (s == NULL || (dx == 0 && dy == 0) || src.w <= 0 || src.h <= 0) {
        return;
    }
    /* Clipped on BOTH ends: the source is read from the surface and the
     * destination is written into the clip, so a block that would run off
     * either is trimmed before a single pixel moves.  The destination
     * answers to the clip like every other way of drawing -- a list that
     * slides its rows must not carry them over its own border. */
    x0 = src.x;
    y0 = src.y;
    x1 = src.x + src.w;
    y1 = src.y + src.h;
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > s->w) { x1 = s->w; }
    if (y1 > s->h) { y1 = s->h; }
    if (x0 + dx < s->clip.x) { x0 = s->clip.x - dx; }
    if (y0 + dy < s->clip.y) { y0 = s->clip.y - dy; }
    if (x1 + dx > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w - dx; }
    if (y1 + dy > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h - dy; }
    w = x1 - x0;
    if (w <= 0 || y1 <= y0) {
        return;
    }
    {
        int sc = scale_of(s), sub;
        long stride = (long)s->w * sc;

        if (dy > 0) {
            /* Downwards: the bottom scanline first, or the copy overwrites
             * the rows it has not read yet. */
            for (y = y1 - 1; y >= y0; y--) {
                for (sub = sc - 1; sub >= 0; sub--) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        } else {
            for (y = y0; y < y1; y++) {
                for (sub = 0; sub < sc; sub++) {
                    memmove(s->px + ((long)(y + dy) * sc + sub) * stride +
                            (long)(x0 + dx) * sc,
                            s->px + ((long)y * sc + sub) * stride +
                            (long)x0 * sc,
                            (size_t)w * sc * sizeof *s->px);
                }
            }
        }
    }
}

void
tiku_fill(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int x, y, x0, y0, x1, y1;

    if (s == NULL) {
        return;
    }
    x0 = (r.x > s->clip.x) ? r.x : s->clip.x;
    y0 = (r.y > s->clip.y) ? r.y : s->clip.y;
    x1 = r.x + r.w;
    y1 = r.y + r.h;
    if (x1 > s->clip.x + s->clip.w) { x1 = s->clip.x + s->clip.w; }
    if (y1 > s->clip.y + s->clip.h) { y1 = s->clip.y + s->clip.h; }
    {
        int sc = scale_of(s);
        long stride = (long)s->w * sc;

        for (y = (long)y0 * sc; y < (long)y1 * sc; y++) {
            tiku_rgb_t *row = s->px + (long)y * stride;
            for (x = (long)x0 * sc; x < (long)x1 * sc; x++) {
                row[x] = c;
            }
        }
    }
}

void
tiku_hline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, len, 1 };
    tiku_fill(s, r, c);
}

void
tiku_vline(tiku_surface_t *s, int x, int y, int len,
                tiku_rgb_t c)
{
    tiku_rect_t r = { x, y, 1, len };
    tiku_fill(s, r, c);
}

void
tiku_frame(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    tiku_hline(s, r.x, r.y, r.w, c);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, c);
    tiku_vline(s, r.x, r.y, r.h, c);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, c);
}

void
tiku_bevel(tiku_surface_t *s, tiku_rect_t r,
                tiku_rgb_t light, tiku_rgb_t shadow)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    tiku_hline(s, r.x, r.y, r.w - 1, light);
    tiku_vline(s, r.x, r.y, r.h - 1, light);
    tiku_hline(s, r.x, r.y + r.h - 1, r.w, shadow);
    tiku_vline(s, r.x + r.w - 1, r.y, r.h, shadow);
}

tiku_rect_t
tiku_inset(tiku_rect_t r, int n)
{
    tiku_rect_t o;

    o.x = r.x + n;
    o.y = r.y + n;
    o.w = r.w - 2 * n;
    o.h = r.h - 2 * n;
    if (o.w < 0) { o.w = 0; }
    if (o.h < 0) { o.h = 0; }
    return o;
}

/*---------------------------------------------------------------------------*/
/* PNG                                                                       */
/*---------------------------------------------------------------------------*/

static unsigned long
png_crc(const unsigned char *b, size_t n, unsigned long crc)
{
    static unsigned long tab[256];
    static int built;
    size_t i;

    if (!built) {
        unsigned long c;
        int k, j;
        for (k = 0; k < 256; k++) {
            c = (unsigned long)k;
            for (j = 0; j < 8; j++) {
                c = (c & 1u) ? (0xEDB88320uL ^ (c >> 1)) : (c >> 1);
            }
            tab[k] = c;
        }
        built = 1;
    }
    for (i = 0; i < n; i++) {
        crc = tab[(crc ^ b[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void
png_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int
png_chunk(FILE *f, const char *tag, const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    unsigned char crcbuf[4];
    unsigned long crc;

    png_be32(hdr, (unsigned long)n);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8u) {
        return -1;
    }
    if (n > 0u && fwrite(data, 1, n, f) != n) {
        return -1;
    }
    crc = png_crc((const unsigned char *)tag, 4u, 0xFFFFFFFFuL);
    crc = png_crc(data, n, crc) ^ 0xFFFFFFFFuL;
    png_be32(crcbuf, crc);
    return (fwrite(crcbuf, 1, 4, f) == 4u) ? 0 : -1;
}

int
tiku_surface_png(const tiku_surface_t *s, const char *path)
{
    FILE *f;
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    size_t rawlen, zlen, off, i;
    unsigned long a = 1, b = 0;
    int y, nw, nh, rc = -1;

    if (s == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    {
        int sc = scale_of(s);
        nw = s->w * sc;
        nh = s->h * sc;
    }
    rawlen = (size_t)nh * (1u + (size_t)nw * 3u);
    raw = malloc(rawlen);
    /* Stored deflate: 5 bytes of block header per 65535, plus zlib framing. */
    zlen = 2u + rawlen + 5u * (rawlen / 65535u + 1u) + 4u;
    z = malloc(zlen);
    if (raw == NULL || z == NULL) {
        goto done;
    }
    off = 0;
    for (y = 0; y < nh; y++) {
        int x;
        raw[off++] = 0;                       /* filter: none */
        for (x = 0; x < nw; x++) {
            tiku_rgb_t c = s->px[(long)y * nw + x];
            raw[off++] = (unsigned char)((c >> 16) & 0xFFu);
            raw[off++] = (unsigned char)((c >> 8) & 0xFFu);
            raw[off++] = (unsigned char)(c & 0xFFu);
        }
    }
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521uL;
        b = (b + a) % 65521uL;
    }
    off = 0;
    z[off++] = 0x78;                          /* zlib: deflate, 32K window */
    z[off++] = 0x01;
    for (i = 0; i < rawlen; ) {
        size_t n = rawlen - i;
        int last;
        if (n > 65535u) { n = 65535u; }
        last = (i + n >= rawlen);
        z[off++] = (unsigned char)(last ? 1 : 0);
        z[off++] = (unsigned char)(n & 0xFFu);
        z[off++] = (unsigned char)((n >> 8) & 0xFFu);
        z[off++] = (unsigned char)(~n & 0xFFu);
        z[off++] = (unsigned char)((~n >> 8) & 0xFFu);
        memcpy(z + off, raw + i, n);
        off += n;
        i += n;
    }
    png_be32(z + off, (b << 16) | a);
    off += 4u;

    if (fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) != 8u) {
        goto done;
    }
    png_be32(ihdr, (unsigned long)nw);
    png_be32(ihdr + 4, (unsigned long)nh);
    ihdr[8] = 8;                              /* bit depth  */
    ihdr[9] = 2;                              /* truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (png_chunk(f, "IHDR", ihdr, sizeof ihdr) != 0 ||
        png_chunk(f, "IDAT", z, off) != 0 ||
        png_chunk(f, "IEND", NULL, 0u) != 0) {
        goto done;
    }
    rc = 0;
done:
    free(raw);
    free(z);
    (void)fclose(f);
    return rc;
}

/** @brief One inverted pixel, honouring the clip. */
static void
invert_pixel(tiku_surface_t *s, int x, int y)
{
    int sc, ry, rx;
    long stride;

    if (s == NULL || x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h ||
        x < 0 || x >= s->w || y < 0 || y >= s->h) {
        return;
    }
    sc = scale_of(s);
    stride = (long)s->w * sc;
    for (ry = 0; ry < sc; ry++) {
        tiku_rgb_t *row = s->px + ((long)y * sc + ry) * stride +
                               (long)x * sc;
        for (rx = 0; rx < sc; rx++) {
            row[rx] = (tiku_rgb_t)(~row[rx] & 0x00FFFFFFu);
        }
    }
}

void
tiku_invert_frame(tiku_surface_t *s, tiku_rect_t r)
{
    int i;

    if (s == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }
    for (i = 0; i < r.w; i++) {
        invert_pixel(s, r.x + i, r.y);
        invert_pixel(s, r.x + i, r.y + r.h - 1);
    }
    for (i = 1; i < r.h - 1; i++) {
        invert_pixel(s, r.x, r.y + i);
        invert_pixel(s, r.x + r.w - 1, r.y + i);
    }
}
