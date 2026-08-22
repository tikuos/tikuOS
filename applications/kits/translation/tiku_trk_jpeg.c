/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_jpeg.c - baseline sequential JPEG: Huffman, dequantise, IDCT,
 * chroma upsample, YCbCr to RGB.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_jpeg.h"

#include <stdlib.h>
#include <string.h>

#define MAXCOMP 4

/** @brief One Huffman table, in the canonical form the standard defines. */
typedef struct {
    unsigned char bits[17];         /* how many codes of each length 1..16 */
    unsigned char huffval[256];
    int           mincode[17];
    int           maxcode[18];
    int           valptr[17];
    int           present;
} jhuff_t;

/** @brief One image component and where its samples live. */
typedef struct {
    int            id, h, v, tq;    /* sampling factors, quant table      */
    int            td, ta;          /* DC and AC table selectors          */
    int            dcpred;
    int            bw, bh;          /* size in blocks, MCU-aligned        */
    unsigned char *pix;             /* bw*8 by bh*8 samples               */
} jcomp_t;

typedef struct {
    const unsigned char *p;
    long                 n, pos;
    unsigned long        buf;
    int                  cnt;
    int                  bad;
} jbits_t;

/** @brief The zig-zag order coefficients arrive in. */
static const unsigned char zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

/**
 * @brief One bit of entropy-coded data.
 *
 * A 0xFF in the stream is followed by a stuffed 0x00; any other byte after
 * 0xFF is a marker, which ends the data.
 */
static int
getbit(jbits_t *b)
{
    if (b->cnt == 0) {
        int c;

        if (b->pos >= b->n) {
            b->bad = 1;
            return 0;
        }
        c = b->p[b->pos++];
        if (c == 0xff) {
            if (b->pos < b->n && b->p[b->pos] == 0x00) {
                b->pos++;
            } else {
                b->bad = 1;         /* a marker: the scan is over */
                return 0;
            }
        }
        b->buf = (unsigned long)c;
        b->cnt = 8;
    }
    b->cnt--;
    return (int)((b->buf >> b->cnt) & 1ul);
}

static int
getbits(jbits_t *b, int n)
{
    int v = 0;

    while (n-- > 0) {
        v = (v << 1) | getbit(b);
    }
    return v;
}

/** @brief Turn a raw magnitude into its signed value (standard F.2.2.1). */
static int
extend(int v, int t)
{
    return (t == 0) ? 0 : ((v < (1 << (t - 1))) ? v - (1 << t) + 1 : v);
}

/** @brief Derive the decoding tables the standard's DECODE needs. */
static void
huff_build(jhuff_t *h)
{
    int code = 0, k = 0, i;

    for (i = 1; i <= 16; i++) {
        h->valptr[i] = k;
        h->mincode[i] = code;
        code += h->bits[i];
        k += h->bits[i];
        h->maxcode[i] = code - 1;
        code <<= 1;
        if (h->bits[i] == 0) {
            h->maxcode[i] = -1;     /* no codes of this length */
        }
    }
    h->maxcode[17] = 0x7fffffff;
}

static int
huff_decode(jbits_t *b, const jhuff_t *h)
{
    int code = getbit(b), l = 1;

    while (l <= 16 && (h->maxcode[l] < 0 || code > h->maxcode[l])) {
        code = (code << 1) | getbit(b);
        l++;
        if (b->bad) {
            return -1;
        }
    }
    if (l > 16) {
        return -1;
    }
    return h->huffval[h->valptr[l] + code - h->mincode[l]];
}

/**
 * @brief Separable 8x8 inverse DCT, integer, rows then columns.
 *
 * The straightforward form rather than a fast factorisation: a thumbnail is
 * decoded once and then cached, so clarity is worth more here than the
 * cycles a butterfly would save.
 */
static void
idct8(const int *in, unsigned char *out, int stride)
{
    static int cs[8][8];
    static int built;
    int tmp[64], u, x, y;

    if (!built) {
        /* cos((2x+1) u pi / 16) * (u ? 1 : 1/sqrt2), scaled by 1024. */
        static const int c[8][8] = {
            { 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024 },
            { 1420, 1204,  805,  283, -283, -805,-1204,-1420 },
            { 1338,  554, -554,-1338,-1338, -554,  554, 1338 },
            { 1204, -283,-1420, -805,  805, 1420,  283,-1204 },
            { 1024,-1024,-1024, 1024, 1024,-1024,-1024, 1024 },
            {  805,-1420,  283, 1204,-1204, -283, 1420, -805 },
            {  554,-1338, 1338, -554, -554, 1338,-1338,  554 },
            {  283, -805, 1204,-1420, 1420,-1204,  805, -283 }
        };

        memcpy(cs, c, sizeof cs);
        built = 1;
    }
    for (y = 0; y < 8; y++) {       /* rows */
        for (x = 0; x < 8; x++) {
            long s = 0;

            for (u = 0; u < 8; u++) {
                s += (long)in[y * 8 + u] * cs[u][x];
            }
            /* Rounded, not truncated: an arithmetic shift of a
             * negative value biases every block half a level
             * dark, and it shows as a flat offset. */
            tmp[y * 8 + x] = (int)((s + 512L) >> 10);
        }
    }
    for (x = 0; x < 8; x++) {       /* columns */
        for (y = 0; y < 8; y++) {
            long s = 0;
            int v;

            for (u = 0; u < 8; u++) {
                s += (long)tmp[u * 8 + x] * cs[u][y];
            }
            v = (int)((s + 4096L) >> 13) + 128;
            out[y * stride + x] = (unsigned char)((v < 0) ? 0 :
                                                  ((v > 255) ? 255 : v));
        }
    }
}

static int
clamp8(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

int
tiku_trk_jpeg_decode(const unsigned char *src, long n, int *ow, int *oh,
                     uint32_t **out)
{
    unsigned short qt[4][64];
    jhuff_t dc[4], ac[4];
    jcomp_t comp[MAXCOMP];
    jbits_t bits;
    long i = 2;
    int w = 0, h = 0, ncomp = 0, hmax = 1, vmax = 1, restart = 0;
    int c, rc = -1, mcux, mcuy, mx, my;
    uint32_t *px = NULL;

    if (src == NULL || n < 4 || src[0] != 0xff || src[1] != 0xd8) {
        return -1;                  /* not a JPEG at all */
    }
    memset(qt, 0, sizeof qt);
    memset(dc, 0, sizeof dc);
    memset(ac, 0, sizeof ac);
    memset(comp, 0, sizeof comp);

    while (i + 4 <= n) {
        int marker, len;

        if (src[i] != 0xff) {
            i++;
            continue;
        }
        marker = src[i + 1];
        i += 2;
        if (marker == 0xd8 || marker == 0x01 ||
            (marker >= 0xd0 && marker <= 0xd7)) {
            continue;               /* standalone markers carry no length */
        }
        if (i + 2 > n) {
            goto done;
        }
        len = (src[i] << 8) | src[i + 1];
        if (len < 2 || i + len > n) {
            goto done;
        }
        if (marker == 0xdb) {                    /* DQT */
            long k = i + 2;

            while (k < i + len) {
                int prec = src[k] >> 4, id = src[k] & 15;
                int z;

                k++;
                if (id > 3 || (prec != 0 && prec != 1)) {
                    goto done;
                }
                for (z = 0; z < 64; z++) {
                    qt[id][z] = prec ? (unsigned short)((src[k] << 8) |
                                                        src[k + 1])
                                     : src[k];
                    k += prec ? 2 : 1;
                }
            }
        } else if (marker == 0xc4) {             /* DHT */
            long k = i + 2;

            while (k < i + len) {
                int cls = src[k] >> 4, id = src[k] & 15, total = 0, z;
                jhuff_t *t;

                k++;
                if (id > 3 || cls > 1) {
                    goto done;
                }
                t = cls ? &ac[id] : &dc[id];
                memset(t, 0, sizeof *t);
                for (z = 1; z <= 16; z++) {
                    t->bits[z] = src[k++];
                    total += t->bits[z];
                }
                if (total > 256 || k + total > i + len) {
                    goto done;
                }
                for (z = 0; z < total; z++) {
                    t->huffval[z] = src[k++];
                }
                huff_build(t);
                t->present = 1;
            }
        } else if (marker == 0xdd) {             /* DRI */
            restart = (src[i + 2] << 8) | src[i + 3];
        } else if (marker == 0xc0 || marker == 0xc1) {   /* SOF0/SOF1 */
            if (src[i + 2] != 8) {
                rc = 1;             /* 12-bit samples */
                goto done;
            }
            h = (src[i + 3] << 8) | src[i + 4];
            w = (src[i + 5] << 8) | src[i + 6];
            ncomp = src[i + 7];
            if (ncomp < 1 || ncomp > MAXCOMP || w <= 0 || h <= 0) {
                goto done;
            }
            for (c = 0; c < ncomp; c++) {
                const unsigned char *e = src + i + 8 + c * 3;

                comp[c].id = e[0];
                comp[c].h = e[1] >> 4;
                comp[c].v = e[1] & 15;
                comp[c].tq = e[2];
                if (comp[c].h < 1 || comp[c].h > 4 ||
                    comp[c].v < 1 || comp[c].v > 4 || comp[c].tq > 3) {
                    goto done;
                }
                if (comp[c].h > hmax) { hmax = comp[c].h; }
                if (comp[c].v > vmax) { vmax = comp[c].v; }
            }
        } else if (marker == 0xc2 || marker == 0xc9 || marker == 0xca ||
                   marker == 0xcb) {
            rc = 1;                 /* progressive or arithmetic */
            goto done;
        } else if (marker == 0xda) {             /* SOS: the scan itself */
            int ns = src[i + 2];

            if (w == 0 || ncomp == 0) {
                goto done;
            }
            for (c = 0; c < ns; c++) {
                int id = src[i + 3 + c * 2], k;

                for (k = 0; k < ncomp; k++) {
                    if (comp[k].id == id) {
                        comp[k].td = src[i + 4 + c * 2] >> 4;
                        comp[k].ta = src[i + 4 + c * 2] & 15;
                    }
                }
            }
            /* Every component gets a plane rounded up to whole MCUs, so the
             * block loop never has to special-case the edges; the extra
             * columns are simply not read back. */
            mcux = (w + hmax * 8 - 1) / (hmax * 8);
            mcuy = (h + vmax * 8 - 1) / (vmax * 8);
            for (c = 0; c < ncomp; c++) {
                comp[c].bw = mcux * comp[c].h;
                comp[c].bh = mcuy * comp[c].v;
                comp[c].pix = calloc((size_t)comp[c].bw * 8u *
                                     (size_t)comp[c].bh * 8u, 1u);
                if (comp[c].pix == NULL) {
                    goto done;
                }
            }
            memset(&bits, 0, sizeof bits);
            bits.p = src;
            bits.n = n;
            bits.pos = i + len;

            for (my = 0; my < mcuy && !bits.bad; my++) {
                for (mx = 0; mx < mcux && !bits.bad; mx++) {
                    int nmcu = my * mcux + mx;

                    if (restart > 0 && nmcu > 0 && nmcu % restart == 0) {
                        /* Resynchronise: the predictors reset and the next
                         * marker is skipped, which is the whole point of
                         * restart intervals. */
                        bits.cnt = 0;
                        while (bits.pos + 1 < n &&
                               !(src[bits.pos] == 0xff &&
                                 src[bits.pos + 1] >= 0xd0 &&
                                 src[bits.pos + 1] <= 0xd7)) {
                            bits.pos++;
                        }
                        bits.pos += 2;
                        bits.bad = 0;
                        for (c = 0; c < ncomp; c++) {
                            comp[c].dcpred = 0;
                        }
                    }
                    for (c = 0; c < ncomp; c++) {
                        int by, bx;

                        for (by = 0; by < comp[c].v; by++) {
                            for (bx = 0; bx < comp[c].h; bx++) {
                                int blk[64], t, k;
                                int row = my * comp[c].v + by;
                                int col = mx * comp[c].h + bx;

                                memset(blk, 0, sizeof blk);
                                t = huff_decode(&bits, &dc[comp[c].td]);
                                if (t < 0) { bits.bad = 1; break; }
                                comp[c].dcpred +=
                                    extend(getbits(&bits, t), t);
                                blk[0] = comp[c].dcpred *
                                         qt[comp[c].tq][0];
                                for (k = 1; k < 64; ) {
                                    int rs = huff_decode(&bits,
                                                         &ac[comp[c].ta]);
                                    int r, sz;

                                    if (rs < 0) { bits.bad = 1; break; }
                                    r = rs >> 4;
                                    sz = rs & 15;
                                    if (sz == 0) {
                                        if (r != 15) { break; }
                                        k += 16;
                                        continue;
                                    }
                                    k += r;
                                    if (k > 63) { break; }
                                    blk[zigzag[k]] =
                                        extend(getbits(&bits, sz), sz) *
                                        qt[comp[c].tq][k];
                                    k++;
                                }
                                idct8(blk, comp[c].pix +
                                          (long)row * 8 * comp[c].bw * 8 +
                                          (long)col * 8,
                                      comp[c].bw * 8);
                            }
                        }
                    }
                }
            }
            /* An entropy stream that ended early still yields a picture --
             * the blocks it did reach are real, and a truncated photo is
             * more use as a thumbnail than none. */
            px = malloc((size_t)w * (size_t)h * sizeof *px);
            if (px == NULL) {
                goto done;
            }
            {
                int y, x;

                for (y = 0; y < h; y++) {
                    for (x = 0; x < w; x++) {
                        int Y, cb = 128, cr = 128, r, g, bl;
                        long sw = (long)comp[0].bw * 8;

                        Y = comp[0].pix[(long)(y * comp[0].v / vmax) * sw +
                                        (x * comp[0].h / hmax)];
                        if (ncomp >= 3) {
                            long w1 = (long)comp[1].bw * 8;
                            long w2 = (long)comp[2].bw * 8;

                            cb = comp[1].pix[
                                (long)(y * comp[1].v / vmax) * w1 +
                                (x * comp[1].h / hmax)];
                            cr = comp[2].pix[
                                (long)(y * comp[2].v / vmax) * w2 +
                                (x * comp[2].h / hmax)];
                        }
                        r = Y + ((91881 * (cr - 128)) >> 16);
                        g = Y - ((22554 * (cb - 128) +
                                  46802 * (cr - 128)) >> 16);
                        bl = Y + ((116130 * (cb - 128)) >> 16);
                        px[(long)y * w + x] = 0xff000000u |
                            ((uint32_t)clamp8(r) << 16) |
                            ((uint32_t)clamp8(g) << 8) |
                            (uint32_t)clamp8(bl);
                    }
                }
            }
            *ow = w;
            *oh = h;
            *out = px;
            px = NULL;
            rc = 0;
            goto done;
        } else if (marker == 0xd9) {
            break;                  /* EOI before any scan */
        }
        i += len;
    }
done:
    for (c = 0; c < MAXCOMP; c++) {
        free(comp[c].pix);
    }
    free(px);
    return rc;
}
