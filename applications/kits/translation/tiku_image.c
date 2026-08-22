/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_image.c - BMP and stored-deflate PNG readers, and thumb scaling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_image.h"

#include "tiku_inflate.h"
#include "tiku_jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tiku_image_is_image(const char *path)
{
    unsigned char magic[8];
    FILE *f;
    size_t n;

    if (path == NULL) {
        return 0;
    }
    /* By CONTENT, not by name.  A file called .txt that holds a PNG is a
     * PNG, and one called .jpg that holds nothing is not a picture; the
     * extension is a hint the file's author may not have meant.  Eight
     * bytes is one block the filesystem has already fetched. */
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(magic, 1u, sizeof magic, f);
    (void)fclose(f);
    if (n >= 8 && magic[0] == 137 && memcmp(magic + 1, "PNG", 3) == 0) {
        return 1;
    }
    if (n >= 3 && magic[0] == 0xff && magic[1] == 0xd8 &&
        magic[2] == 0xff) {
        return 1;
    }
    if (n >= 2 && magic[0] == 'B' && magic[1] == 'M') {
        return 1;
    }
    return 0;
}

/** @brief Read a whole file.  Caller frees.  @return length, or -1. */
static long
slurp(const char *path, unsigned char **out)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;

    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return -1;
    }
    buf = malloc((size_t)n + 1u);
    if (buf == NULL) {
        (void)fclose(f);
        return -1;
    }
    if (fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    *out = buf;
    return n;
}

static uint32_t
be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t
le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*---------------------------------------------------------------------------*/
/* BMP: uncompressed 24- and 32-bit, bottom-up or top-down                   */
/*---------------------------------------------------------------------------*/

static tiku_image_err_t
load_bmp(const unsigned char *b, long n, tiku_image_t *out)
{
    uint32_t off, hdr, bpp, comp;
    int32_t w, h;
    int flip, y, x;
    long stride;

    if (n < 54) {
        return TIKU_IMG_BAD;
    }
    off = le32(b + 10);
    hdr = le32(b + 14);
    w = (int32_t)le32(b + 18);
    h = (int32_t)le32(b + 22);
    bpp = (uint32_t)(b[28] | (b[29] << 8));
    comp = le32(b + 30);
    if (hdr < 40u || (bpp != 24u && bpp != 32u) || comp != 0u) {
        return TIKU_IMG_UNSUPPORTED;
    }
    /* A negative height means the rows are stored top-down. */
    flip = (h > 0);
    if (h < 0) {
        h = -h;
    }
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
        return TIKU_IMG_BAD;
    }
    /* Rows are padded to a multiple of four bytes. */
    stride = ((long)w * (long)(bpp / 8u) + 3L) & ~3L;
    if ((long)off + stride * h > n) {
        return TIKU_IMG_BAD;
    }
    out->px = malloc((size_t)w * (size_t)h * sizeof *out->px);
    if (out->px == NULL) {
        return TIKU_IMG_BAD;
    }
    out->w = w;
    out->h = h;
    for (y = 0; y < h; y++) {
        const unsigned char *row = b + off +
            (long)(flip ? (h - 1 - y) : y) * stride;

        for (x = 0; x < w; x++) {
            const unsigned char *p = row + (long)x * (long)(bpp / 8u);
            uint32_t a = (bpp == 32u) ? p[3] : 0xffu;

            out->px[(long)y * w + x] = (a << 24) | ((uint32_t)p[2] << 16) |
                                       ((uint32_t)p[1] << 8) | p[0];
        }
    }
    return TIKU_IMG_OK;
}

/*---------------------------------------------------------------------------*/
/* PNG, stored-deflate only                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Undo one scanline's PNG filter, in place. */
static void
unfilter(unsigned char *cur, const unsigned char *prev, long len, int bpp,
         int type)
{
    long i;

    for (i = 0; i < len; i++) {
        int a = (i >= bpp) ? cur[i - bpp] : 0;
        int b = (prev != NULL) ? prev[i] : 0;
        int c = (prev != NULL && i >= bpp) ? prev[i - bpp] : 0;
        int v = cur[i];

        switch (type) {
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: {
            int p = a + b - c, pa = abs(p - a), pb = abs(p - b);
            int pc = abs(p - c);

            v += (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
            break;
        }
        default: break;
        }
        cur[i] = (unsigned char)v;
    }
}

/** @brief One sample of @p depth bits at index @p i of a packed row. */
static unsigned
sample(const unsigned char *row, int depth, long i)
{
    switch (depth) {
    case 8:  return row[i];
    case 4:  return (row[i >> 1] >> ((i & 1) ? 0 : 4)) & 0x0fu;
    case 2:  return (row[i >> 2] >> (6 - 2 * (i & 3))) & 0x03u;
    case 1:  return (row[i >> 3] >> (7 - (i & 7))) & 0x01u;
    default: return row[i];
    }
}

/** @brief Spread an n-bit sample over the full 0..255 range. */
static unsigned
scale_up(unsigned v, int depth)
{
    switch (depth) {
    case 8:  return v;
    case 4:  return v * 17u;                 /* 0..15  -> 0..255 */
    case 2:  return v * 85u;                 /* 0..3   -> 0..255 */
    case 1:  return v ? 255u : 0u;
    default: return v;
    }
}

/** @brief The seven Adam7 passes: x origin, y origin, x step, y step. */
static const int adam7[7][4] = {
    { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 },
    { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

/** @brief Everything one pass of a PNG needs in order to place its pixels. */
typedef struct {
    int             colour, depth, bpp, chan;
    const uint32_t *pal;
    int             npal;
} png_fmt_t;

/**
 * @brief Unfilter and place one pass (or the whole image, uninterlaced).
 *
 * @return bytes consumed, or -1.
 */
static long
png_pass(const unsigned char *raw, long avail, int pw, int ph,
         const png_fmt_t *f, tiku_image_t *out, int x0, int y0,
         int dx, int dy)
{
    long rowbytes = ((long)pw * f->chan * f->depth + 7L) / 8L;
    long used = 0;
    unsigned char *prev = NULL, *cur = NULL;
    int y;

    if (pw <= 0 || ph <= 0) {
        return 0;
    }
    prev = calloc((size_t)rowbytes, 1u);
    cur = malloc((size_t)rowbytes);
    if (prev == NULL || cur == NULL) {
        free(prev);
        free(cur);
        return -1;
    }
    for (y = 0; y < ph; y++) {
        int type, x;

        if (used + 1 + rowbytes > avail) {
            free(prev); free(cur);
            return -1;
        }
        type = raw[used++];
        memcpy(cur, raw + used, (size_t)rowbytes);
        used += rowbytes;
        unfilter(cur, (y > 0) ? prev : NULL, rowbytes, f->bpp, type);

        for (x = 0; x < pw; x++) {
            int ox = x0 + x * dx, oy = y0 + y * dy;
            unsigned r, g, bl, a = 255u;

            if (ox >= out->w || oy >= out->h) {
                continue;
            }
            switch (f->colour) {
            case 0:                 /* greyscale */
                r = g = bl = scale_up(sample(cur, f->depth, x), f->depth);
                break;
            case 4:                 /* greyscale + alpha */
                r = g = bl = scale_up(sample(cur, f->depth, (long)x * 2),
                                      f->depth);
                a = scale_up(sample(cur, f->depth, (long)x * 2 + 1),
                             f->depth);
                break;
            case 3: {               /* palette */
                unsigned idx = sample(cur, f->depth, x);
                uint32_t e = (idx < (unsigned)f->npal) ? f->pal[idx]
                                                       : 0xff000000u;

                r = (e >> 16) & 0xffu;
                g = (e >> 8) & 0xffu;
                bl = e & 0xffu;
                a = (e >> 24) & 0xffu;
                break;
            }
            case 6:                 /* truecolour + alpha */
                r = cur[(long)x * 4];
                g = cur[(long)x * 4 + 1];
                bl = cur[(long)x * 4 + 2];
                a = cur[(long)x * 4 + 3];
                break;
            default:                /* truecolour */
                r = cur[(long)x * 3];
                g = cur[(long)x * 3 + 1];
                bl = cur[(long)x * 3 + 2];
                break;
            }
            out->px[(long)oy * out->w + ox] =
                (a << 24) | (r << 16) | (g << 8) | bl;
        }
        {
            unsigned char *t = prev;

            prev = cur;
            cur = t;
        }
    }
    free(prev);
    free(cur);
    return used;
}

static tiku_image_err_t
load_png(const unsigned char *b, long n, tiku_image_t *out)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char *idat = NULL, *raw = NULL;
    uint32_t pal[256];
    long ilen = 0, i = 8, rawlen, got;
    int w = 0, h = 0, interlace = 0, npal = 0, p;
    png_fmt_t f;
    tiku_image_err_t rc = TIKU_IMG_BAD;

    memset(&f, 0, sizeof f);
    for (p = 0; p < 256; p++) {
        pal[p] = 0xff000000u;
    }
    if (n < 8 || memcmp(b, sig, 8) != 0) {
        return TIKU_IMG_NOT_IMAGE;
    }
    while (i + 8 <= n) {
        uint32_t len = be32(b + i);
        const char *type = (const char *)(b + i + 4);

        if ((long)len > n || i + 12 + (long)len > n) {
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13u) {
            w = (int)be32(b + i + 8);
            h = (int)be32(b + i + 12);
            f.depth = b[i + 16];
            f.colour = b[i + 17];
            interlace = b[i + 20];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            uint32_t k;

            npal = (int)(len / 3u);
            if (npal > 256) { npal = 256; }
            for (k = 0; k < (uint32_t)npal; k++) {
                pal[k] = 0xff000000u |
                         ((uint32_t)b[i + 8 + k * 3u] << 16) |
                         ((uint32_t)b[i + 9 + k * 3u] << 8) |
                         b[i + 10 + k * 3u];
            }
        } else if (memcmp(type, "tRNS", 4) == 0 && f.colour == 3) {
            uint32_t k;

            /* Per-entry alpha for a palette: the only transparency a
             * palette image can carry, and what makes a cut-out icon a
             * cut-out rather than a white square. */
            for (k = 0; k < len && k < 256u; k++) {
                pal[k] = (pal[k] & 0x00ffffffu) |
                         ((uint32_t)b[i + 8 + k] << 24);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            unsigned char *grown = realloc(idat, (size_t)(ilen + len));

            if (grown == NULL) {
                free(idat);
                return TIKU_IMG_BAD;
            }
            idat = grown;
            memcpy(idat + ilen, b + i + 8, len);
            ilen += (long)len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        i += 12 + (long)len;
    }
    if (w <= 0 || h <= 0 || idat == NULL || w > 30000 || h > 30000) {
        free(idat);
        return TIKU_IMG_BAD;
    }
    switch (f.colour) {
    case 0: f.chan = 1; break;
    case 2: f.chan = 3; break;
    case 3: f.chan = 1; break;
    case 4: f.chan = 2; break;
    case 6: f.chan = 4; break;
    default: free(idat); return TIKU_IMG_BAD;
    }
    /* Truecolour and alpha channels are 8 bits here; the sub-byte depths
     * belong to greyscale and palette, which is where they buy anything. */
    if ((f.colour == 2 || f.colour == 6) && f.depth != 8) {
        free(idat);
        return TIKU_IMG_UNSUPPORTED;
    }
    if (f.depth != 1 && f.depth != 2 && f.depth != 4 && f.depth != 8) {
        free(idat);
        return TIKU_IMG_UNSUPPORTED;   /* 16 bits per sample */
    }
    if (f.colour == 3 && npal == 0) {
        free(idat);
        return TIKU_IMG_BAD;
    }
    f.pal = pal;
    f.npal = npal;
    f.bpp = (f.chan * f.depth + 7) / 8;
    if (f.bpp < 1) { f.bpp = 1; }

    /* Room for every pass's rows plus one filter byte each; the
     * uninterlaced case is the same sum with one pass. */
    rawlen = ((long)w * f.chan * f.depth / 8L + f.chan + 1L) * h + h + 64L;
    rawlen *= 2L;
    raw = malloc((size_t)rawlen);
    out->px = calloc((size_t)w * (size_t)h, sizeof *out->px);
    if (raw == NULL || out->px == NULL) {
        goto done;
    }
    out->w = w;
    out->h = h;
    got = tiku_inflate_zlib(idat, ilen, raw, rawlen);
    if (got < 0) {
        goto done;
    }
    if (!interlace) {
        if (png_pass(raw, got, w, h, &f, out, 0, 0, 1, 1) < 0) {
            goto done;
        }
    } else {
        long used = 0;

        /* Adam7: seven sub-images, each filtered on its own rows, which is
         * why the pass has to know its own width rather than the image's. */
        for (p = 0; p < 7; p++) {
            int x0 = adam7[p][0], y0 = adam7[p][1];
            int dx = adam7[p][2], dy = adam7[p][3];
            int pw = (w - x0 + dx - 1) / dx;
            int ph = (h - y0 + dy - 1) / dy;
            long k = png_pass(raw + used, got - used, pw, ph, &f, out,
                              x0, y0, dx, dy);

            if (k < 0) {
                goto done;
            }
            used += k;
        }
    }
    rc = TIKU_IMG_OK;
done:
    free(idat);
    free(raw);
    if (rc != TIKU_IMG_OK) {
        tiku_image_free(out);
    }
    return rc;
}

tiku_image_err_t
tiku_image_load(const char *path, tiku_image_t *out)
{
    unsigned char *b = NULL;
    long n;
    tiku_image_err_t rc;

    if (path == NULL || out == NULL) {
        return TIKU_IMG_BAD;
    }
    n = slurp(path, &b);
    if (n < 0) {
        return TIKU_IMG_BAD;
    }
    memset(out, 0, sizeof *out);
    if (n >= 2 && b[0] == 'B' && b[1] == 'M') {
        rc = load_bmp(b, n, out);
    } else if (n >= 8 && b[0] == 137 && b[1] == 'P') {
        rc = load_png(b, n, out);
    } else if (n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) {
        int jw = 0, jh = 0, jrc;
        uint32_t *jpx = NULL;

        jrc = tiku_jpeg_decode(b, n, &jw, &jh, &jpx);
        if (jrc == 0) {
            out->w = jw;
            out->h = jh;
            out->px = jpx;
            rc = TIKU_IMG_OK;
        } else {
            rc = (jrc == 1) ? TIKU_IMG_UNSUPPORTED : TIKU_IMG_BAD;
        }
    } else {
        rc = TIKU_IMG_NOT_IMAGE;
    }
    free(b);
    return rc;
}

void
tiku_image_free(tiku_image_t *im)
{
    if (im != NULL) {
        free(im->px);
        im->px = NULL;
        im->w = 0;
        im->h = 0;
    }
}

int
tiku_image_thumb(const tiku_image_t *src, int size,
                     tiku_image_t *out)
{
    int tw, th, ox, oy, y, x;

    if (src == NULL || out == NULL || src->px == NULL || size <= 0 ||
        src->w <= 0 || src->h <= 0) {
        return -1;
    }
    /* The longer side sets the scale, so the whole picture fits and its
     * shape survives -- a stretched thumbnail is harder to recognise than
     * a small one. */
    if (src->w >= src->h) {
        tw = size;
        th = (int)(((long)src->h * size + src->w / 2) / src->w);
    } else {
        th = size;
        tw = (int)(((long)src->w * size + src->h / 2) / src->h);
    }
    if (tw < 1) { tw = 1; }
    if (th < 1) { th = 1; }
    ox = (size - tw) / 2;
    oy = (size - th) / 2;

    out->w = size;
    out->h = size;
    out->px = calloc((size_t)size * (size_t)size, sizeof *out->px);
    if (out->px == NULL) {
        return -1;
    }
    /* calloc leaves the margins at 0: fully transparent, not black. */
    for (y = 0; y < th; y++) {
        long sy = (long)y * src->h / th;

        for (x = 0; x < tw; x++) {
            long sx = (long)x * src->w / tw;

            out->px[(long)(y + oy) * size + (x + ox)] =
                src->px[sy * src->w + sx];
        }
    }
    return 0;
}
