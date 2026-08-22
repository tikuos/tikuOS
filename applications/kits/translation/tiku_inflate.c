/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_inflate.c - DEFLATE: stored, fixed and dynamic Huffman blocks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_inflate.h"

#include <string.h>

#define MAXBITS   15                /* longest Huffman code               */
#define MAXLCODES 286               /* literal/length codes               */
#define MAXDCODES 30                /* distance codes                     */
#define MAXCODES  (MAXLCODES + MAXDCODES)
#define FIXLCODES 288               /* the fixed table's literal codes     */

/** @brief A bit-at-a-time reader; DEFLATE packs least significant first. */
typedef struct {
    const unsigned char *src;
    long                 slen;
    long                 pos;
    unsigned long        bitbuf;
    int                  bitcnt;
    unsigned char       *out;
    long                 omax;
    long                 owr;
    int                  bad;
} state_t;

/**
 * @brief A canonical Huffman table: how many codes of each length, and the
 * symbols in code order.
 *
 * Held this way rather than as a tree because the decode below walks
 * lengths, not nodes: no allocation, and the whole table is two arrays.
 */
typedef struct {
    short count[MAXBITS + 1];
    short symbol[MAXCODES];
} huff_t;

/** @brief Pull @p need bits.  @return the value, or -1 past the end. */
static long
bits(state_t *s, int need)
{
    unsigned long val = s->bitbuf;

    while (s->bitcnt < need) {
        if (s->pos >= s->slen) {
            s->bad = 1;
            return -1;
        }
        val |= (unsigned long)s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (long)(val & ((1ul << need) - 1ul));
}

/**
 * @brief Decode one symbol.
 *
 * Walks code lengths shortest-first, tracking the first code of each length
 * and how many there are; a code that falls inside a length's run indexes
 * straight into the symbol array.
 */
static int
decode(state_t *s, const huff_t *h)
{
    int len, code = 0, first = 0, index = 0;

    for (len = 1; len <= MAXBITS; len++) {
        long b = bits(s, 1);

        if (b < 0) {
            return -1;
        }
        code |= (int)b;
        if (code - first < h->count[len]) {
            return h->symbol[index + (code - first)];
        }
        index += h->count[len];
        first += h->count[len];
        first <<= 1;
        code <<= 1;
    }
    return -1;                      /* no code matched: malformed */
}

/**
 * @brief Build a table from code lengths.
 *
 * @return 0 for a complete code, >0 for an incomplete one (legal only for
 *         a single-symbol distance table), -1 for an over-subscribed one.
 */
static int
construct(huff_t *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];

    for (len = 0; len <= MAXBITS; len++) {
        h->count[len] = 0;
    }
    for (symbol = 0; symbol < n; symbol++) {
        h->count[length[symbol]]++;
    }
    if (h->count[0] == n) {
        return 0;                   /* no codes at all: complete and empty */
    }
    /* A code is over-subscribed when its lengths claim more of the code
     * space than exists -- the one malformity that would otherwise decode
     * into nonsense rather than failing. */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return -1;
        }
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) {
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    }
    for (symbol = 0; symbol < n; symbol++) {
        if (length[symbol] != 0) {
            h->symbol[offs[length[symbol]]++] = (short)symbol;
        }
    }
    return left;
}

/** @brief One length/distance pair, or a run of literals, into out. */
static int
codes(state_t *s, const huff_t *lencode, const huff_t *distcode)
{
    /* RFC 1951 section 3.2.5: the length and distance bases, and how many
     * extra bits each carries. */
    static const short lens[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
        59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const short lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
        4, 5, 5, 5, 5, 0
    };
    static const short dists[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
        513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
        24577
    };
    static const short dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
        10, 10, 11, 11, 12, 12, 13, 13
    };
    int symbol;

    do {
        symbol = decode(s, lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 256) {
            if (s->owr >= s->omax) {
                return -1;
            }
            s->out[s->owr++] = (unsigned char)symbol;
        } else if (symbol > 256) {
            long len, dist, e;

            symbol -= 257;
            if (symbol >= 29) {
                return -1;
            }
            e = bits(s, lext[symbol]);
            if (e < 0) {
                return -1;
            }
            len = lens[symbol] + e;

            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30) {
                return -1;
            }
            e = bits(s, dext[symbol]);
            if (e < 0) {
                return -1;
            }
            dist = dists[symbol] + e;
            /* The window is the output written so far, so a distance
             * reaching before the start is the check that stops a crafted
             * stream reading memory it does not own. */
            if (dist > s->owr || s->owr + len > s->omax) {
                return -1;
            }
            while (len-- > 0) {
                s->out[s->owr] = s->out[s->owr - dist];
                s->owr++;
            }
        }
    } while (symbol != 256);
    return 0;
}

/** @brief A block using the built-in tables (RFC 1951 section 3.2.6). */
static int
fixed_block(state_t *s)
{
    static huff_t lencode, distcode;
    static int built;

    if (!built) {
        short lengths[FIXLCODES];
        int symbol;

        for (symbol = 0; symbol < 144; symbol++)   { lengths[symbol] = 8; }
        for (; symbol < 256; symbol++)             { lengths[symbol] = 9; }
        for (; symbol < 280; symbol++)             { lengths[symbol] = 7; }
        for (; symbol < FIXLCODES; symbol++)       { lengths[symbol] = 8; }
        (void)construct(&lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++) { lengths[symbol] = 5; }
        (void)construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

/** @brief A block carrying its own tables (RFC 1951 section 3.2.7). */
static int
dynamic_block(state_t *s)
{
    /* The order the code-length code's own lengths arrive in: the least
     * useful lengths last, so a short table can stop early. */
    static const short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    huff_t lencode, distcode;
    short lengths[MAXCODES];
    long nlen, ndist, ncode;
    int index, err;

    nlen = bits(s, 5);
    ndist = bits(s, 5);
    ncode = bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) {
        return -1;
    }
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) {
        return -1;
    }
    for (index = 0; index < ncode; index++) {
        long v = bits(s, 3);

        if (v < 0) {
            return -1;
        }
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++) {
        lengths[order[index]] = 0;
    }
    if (construct(&lencode, lengths, 19) != 0) {
        return -1;                  /* the code-length code must be complete */
    }
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        long len;

        if (symbol < 0) {
            return -1;
        }
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0) {
                return -1;          /* nothing to repeat */
            }
            symbol = lengths[index - 1];
            len = bits(s, 2);
            if (len < 0) { return -1; }
            len += 3;
        } else if (symbol == 17) {
            symbol = 0;
            len = bits(s, 3);
            if (len < 0) { return -1; }
            len += 3;
        } else {
            symbol = 0;
            len = bits(s, 7);
            if (len < 0) { return -1; }
            len += 11;
        }
        if (index + len > nlen + ndist) {
            return -1;
        }
        while (len-- > 0) {
            lengths[index++] = (short)symbol;
        }
    }
    if (lengths[256] == 0) {
        return -1;                  /* no end-of-block code */
    }
    err = construct(&lencode, lengths, (int)nlen);
    if (err < 0 || (err > 0 && nlen != 1 + lencode.count[0])) {
        return -1;
    }
    err = construct(&distcode, lengths + nlen, (int)ndist);
    if (err < 0 || (err > 0 && ndist != 1 + distcode.count[0])) {
        return -1;
    }
    return codes(s, &lencode, &distcode);
}

/** @brief An uncompressed block: byte-aligned, with a length and its NOT. */
static int
stored_block(state_t *s)
{
    long len;

    s->bitbuf = 0;
    s->bitcnt = 0;                  /* stored blocks start on a byte */
    if (s->pos + 4 > s->slen) {
        return -1;
    }
    len = (long)s->src[s->pos] | ((long)s->src[s->pos + 1] << 8);
    if (s->src[s->pos + 2] != (~s->src[s->pos] & 0xffu) ||
        s->src[s->pos + 3] != (~s->src[s->pos + 1] & 0xffu)) {
        return -1;                  /* the length disagrees with its NOT */
    }
    s->pos += 4;
    if (s->pos + len > s->slen || s->owr + len > s->omax) {
        return -1;
    }
    memcpy(s->out + s->owr, s->src + s->pos, (size_t)len);
    s->pos += len;
    s->owr += len;
    return 0;
}

long
tiku_inflate(const unsigned char *src, long slen, unsigned char *out,
                 long omax)
{
    state_t s;
    int last;

    if (src == NULL || out == NULL || slen < 0 || omax < 0) {
        return -1;
    }
    memset(&s, 0, sizeof s);
    s.src = src;
    s.slen = slen;
    s.out = out;
    s.omax = omax;
    do {
        long final = bits(&s, 1);
        long type = bits(&s, 2);
        int err;

        if (final < 0 || type < 0) {
            return -1;
        }
        last = (int)final;
        switch (type) {
        case 0:  err = stored_block(&s);  break;
        case 1:  err = fixed_block(&s);   break;
        case 2:  err = dynamic_block(&s); break;
        default: return -1;         /* type 3 is reserved */
        }
        if (err != 0 || s.bad) {
            return -1;
        }
    } while (!last);
    return s.owr;
}

long
tiku_inflate_zlib(const unsigned char *src, long slen, unsigned char *out,
                      long omax)
{
    if (src == NULL || slen < 2) {
        return -1;
    }
    /* CMF/FLG: deflate with a window we do not need to honour (the whole
     * output is in memory), no preset dictionary, and the pair must be a
     * multiple of 31. */
    if ((src[0] & 0x0fu) != 8u || (src[1] & 0x20u) != 0u ||
        (((unsigned)src[0] << 8) | src[1]) % 31u != 0u) {
        return -1;
    }
    return tiku_inflate(src + 2, slen - 2, out, omax);
}
