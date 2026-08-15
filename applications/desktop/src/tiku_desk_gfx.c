/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_gfx.c - surface primitives and the PNG writer.
 *
 * Every primitive clips against the surface's rectangle, so callers draw in
 * widget coordinates and never test bounds themselves.  The PNG writer emits
 * stored (uncompressed) deflate blocks: a few lines of code instead of a zlib
 * dependency, and screenshots are not a size-critical path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_desk_scale_pixels(tiku_desk_rgb_t *destination,
                       int destination_width, int destination_height,
                       const tiku_desk_rgb_t *source,
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
            tiku_desk_rgb_t *row = destination +
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

tiku_desk_surface_t *
tiku_desk_surface_new(int w, int h, tiku_desk_rgb_t bg)
{
    tiku_desk_surface_t *s;
    long i, n = (long)w * (long)h;

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
    tiku_desk_clip_reset(s);
    return s;
}

int
tiku_desk_surface_resize(tiku_desk_surface_t *s, int w, int h,
                         tiku_desk_rgb_t bg)
{
    tiku_desk_rgb_t *pixels;
    size_t i, count;

    if (s == NULL || w <= 0 || h <= 0 ||
        (size_t)w > SIZE_MAX / sizeof *pixels / (size_t)h) {
        return -1;
    }
    count = (size_t)w * (size_t)h;
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
    tiku_desk_clip_reset(s);
    return 0;
}

void
tiku_desk_surface_free(tiku_desk_surface_t *s)
{
    if (s != NULL) {
        free(s->px);
        free(s);
    }
}

void
tiku_desk_clip_reset(tiku_desk_surface_t *s)
{
    if (s != NULL) {
        s->clip.x = 0;
        s->clip.y = 0;
        s->clip.w = s->w;
        s->clip.h = s->h;
    }
}

void
tiku_desk_clip_set(tiku_desk_surface_t *s, tiku_desk_rect_t r)
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

tiku_desk_rgb_t
tiku_desk_tint(tiku_desk_rgb_t c, float tint)
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
    return TIKU_DESK_RGB(out[0], out[1], out[2]);
}

void
tiku_desk_pixel(tiku_desk_surface_t *s, int x, int y, tiku_desk_rgb_t c)
{
    if (s == NULL || x < s->clip.x || y < s->clip.y ||
        x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h) {
        return;
    }
    s->px[(long)y * s->w + x] = c;
}

void
tiku_desk_blit(tiku_desk_surface_t *dst, int x, int y,
               const tiku_desk_surface_t *src)
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
    for (row = y0; row < y1; row++) {
        memcpy(dst->px + (long)row * dst->w + x0,
               src->px + (long)(row - y) * src->w + (x0 - x),
               (size_t)(x1 - x0) * sizeof *dst->px);
    }
}

void
tiku_desk_copy_bits(tiku_desk_surface_t *s, tiku_desk_rect_t src, int dx,
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
    if (dy > 0) {
        /* Downwards: the bottom scanline first, or the copy overwrites
         * the rows it has not read yet. */
        for (y = y1 - 1; y >= y0; y--) {
            memmove(s->px + (long)(y + dy) * s->w + x0 + dx,
                    s->px + (long)y * s->w + x0,
                    (size_t)w * sizeof *s->px);
        }
    } else {
        for (y = y0; y < y1; y++) {
            memmove(s->px + (long)(y + dy) * s->w + x0 + dx,
                    s->px + (long)y * s->w + x0,
                    (size_t)w * sizeof *s->px);
        }
    }
}

void
tiku_desk_fill(tiku_desk_surface_t *s, tiku_desk_rect_t r, tiku_desk_rgb_t c)
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
    for (y = y0; y < y1; y++) {
        tiku_desk_rgb_t *row = s->px + (long)y * s->w;
        for (x = x0; x < x1; x++) {
            row[x] = c;
        }
    }
}

void
tiku_desk_hline(tiku_desk_surface_t *s, int x, int y, int len,
                tiku_desk_rgb_t c)
{
    tiku_desk_rect_t r = { x, y, len, 1 };
    tiku_desk_fill(s, r, c);
}

void
tiku_desk_vline(tiku_desk_surface_t *s, int x, int y, int len,
                tiku_desk_rgb_t c)
{
    tiku_desk_rect_t r = { x, y, 1, len };
    tiku_desk_fill(s, r, c);
}

void
tiku_desk_frame(tiku_desk_surface_t *s, tiku_desk_rect_t r, tiku_desk_rgb_t c)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    tiku_desk_hline(s, r.x, r.y, r.w, c);
    tiku_desk_hline(s, r.x, r.y + r.h - 1, r.w, c);
    tiku_desk_vline(s, r.x, r.y, r.h, c);
    tiku_desk_vline(s, r.x + r.w - 1, r.y, r.h, c);
}

void
tiku_desk_bevel(tiku_desk_surface_t *s, tiku_desk_rect_t r,
                tiku_desk_rgb_t light, tiku_desk_rgb_t shadow)
{
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    tiku_desk_hline(s, r.x, r.y, r.w - 1, light);
    tiku_desk_vline(s, r.x, r.y, r.h - 1, light);
    tiku_desk_hline(s, r.x, r.y + r.h - 1, r.w, shadow);
    tiku_desk_vline(s, r.x + r.w - 1, r.y, r.h, shadow);
}

tiku_desk_rect_t
tiku_desk_inset(tiku_desk_rect_t r, int n)
{
    tiku_desk_rect_t o;

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
tiku_desk_surface_png(const tiku_desk_surface_t *s, const char *path)
{
    FILE *f;
    unsigned char ihdr[13];
    unsigned char *raw, *z;
    size_t rawlen, zlen, off, i;
    unsigned long a = 1, b = 0;
    int y, rc = -1;

    if (s == NULL || path == NULL) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    rawlen = (size_t)s->h * (1u + (size_t)s->w * 3u);
    raw = malloc(rawlen);
    /* Stored deflate: 5 bytes of block header per 65535, plus zlib framing. */
    zlen = 2u + rawlen + 5u * (rawlen / 65535u + 1u) + 4u;
    z = malloc(zlen);
    if (raw == NULL || z == NULL) {
        goto done;
    }
    off = 0;
    for (y = 0; y < s->h; y++) {
        int x;
        raw[off++] = 0;                       /* filter: none */
        for (x = 0; x < s->w; x++) {
            tiku_desk_rgb_t c = s->px[(long)y * s->w + x];
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
    png_be32(ihdr, (unsigned long)s->w);
    png_be32(ihdr + 4, (unsigned long)s->h);
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
invert_pixel(tiku_desk_surface_t *s, int x, int y)
{
    tiku_desk_rgb_t *px;

    if (s == NULL || x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h ||
        x < 0 || x >= s->w || y < 0 || y >= s->h) {
        return;
    }
    px = &s->px[(size_t)y * (size_t)s->w + (size_t)x];
    *px = (tiku_desk_rgb_t)(~(*px) & 0x00FFFFFFu);
}

void
tiku_desk_invert_frame(tiku_desk_surface_t *s, tiku_desk_rect_t r)
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
