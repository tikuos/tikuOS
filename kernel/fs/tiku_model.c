/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.c - Tiku model loader.  See tiku_model.h for the model and the why.
 *
 * ON-FILE LAYOUT of a RELOC model, as tools/axonpack.py writes it.  A 64-byte
 * header of little-endian u32s, then four blobs at the offsets it names:
 *
 *    0  magic 'AXM1'      12  weights_off       28  sites_off
 *    4  version           16  weights_len       32  nsites
 *    8  hdr_bytes (64)    20  cmd_off           36  syms_off
 *                         24  cmd_len           40  nsyms
 *   44  crc32(weights)    48  crc32(cmd)        52  crc32(sites+syms)
 *
 *   [weights] [cmd, UNRELOCATED] [nsites x (u32 off, u32 sym)] [names\0...]
 *
 * The command buffer ships unrelocated because ARM's REL form already stores
 * each site's addend in place, so patching is one addition per site rather than
 * a rewrite.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"

#include <string.h>

#include "kernel/memory/tiku_nvm_mirror.h"   /* tiku_nvm_crc32 */

/*---------------------------------------------------------------------------*/
/* FILE FORMAT                                                               */
/*---------------------------------------------------------------------------*/

#define AXM_MAGIC        0x314D5841u   /* 'AXM1' little-endian */
/*
 * Version 2 adds the third CRC, over the relocation table and symbol names.
 *
 * v1 covered the weights and the command buffer but left the TABLE unchecked --
 * and the table is the one part of the file that says "write four bytes here".
 * Structural validation bounds every entry, so a corrupt table cannot escape
 * the command buffer; but an in-bounds corrupt OFFSET silently patches the
 * wrong word, and the symptom is wrong inference rather than a fault.  A
 * checksum is the only thing that catches that, so the format carries one and
 * v1 files are refused rather than read with a gap.
 */
#define AXM_VERSION      2u
#define AXM_HDR_BYTES    64u
#define AXM_SITE_BYTES   8u            /* u32 offset + u32 symbol index */

/* Header word indices (byte offset = index * 4). */
#define AXM_W_MAGIC      0u
#define AXM_W_VERSION    1u
#define AXM_W_HDRLEN     2u
#define AXM_W_WOFF       3u
#define AXM_W_WLEN       4u
#define AXM_W_COFF       5u
#define AXM_W_CLEN       6u
#define AXM_W_ROFF       7u
#define AXM_W_NSITES     8u
#define AXM_W_SOFF       9u
#define AXM_W_NSYMS      10u
#define AXM_W_CRC_W      11u
#define AXM_W_CRC_C      12u
#define AXM_W_CRC_T      13u

/*---------------------------------------------------------------------------*/
/* SYMBOL REGISTRY                                                           */
/*---------------------------------------------------------------------------*/

typedef struct {
    char      name[TIKU_MODEL_SYM_NAME_MAX];
    uintptr_t addr;
    uint8_t   used;
} model_sym_t;

static model_sym_t model_syms[TIKU_MODEL_SYM_MAX];

int tiku_model_sym_register(const char *name, uintptr_t addr)
{
    size_t n;
    unsigned i, free_slot = TIKU_MODEL_SYM_MAX;

    if (name == NULL || name[0] == '\0') {
        return TIKU_MODEL_ERR_PARAM;
    }
    n = strlen(name);
    if (n >= sizeof model_syms[0].name) {
        return TIKU_MODEL_ERR_PARAM;
    }
    for (i = 0; i < TIKU_MODEL_SYM_MAX; i++) {
        if (model_syms[i].used && strcmp(model_syms[i].name, name) == 0) {
            model_syms[i].addr = addr;      /* re-register: replace */
            return TIKU_MODEL_OK;
        }
        if (!model_syms[i].used && free_slot == TIKU_MODEL_SYM_MAX) {
            free_slot = i;
        }
    }
    if (free_slot == TIKU_MODEL_SYM_MAX) {
        return TIKU_MODEL_ERR_FULL;
    }
    memset(model_syms[free_slot].name, 0, sizeof model_syms[free_slot].name);
    memcpy(model_syms[free_slot].name, name, n);
    model_syms[free_slot].addr = addr;
    model_syms[free_slot].used = 1u;
    return TIKU_MODEL_OK;
}

void tiku_model_sym_reset(void)
{
    memset(model_syms, 0, sizeof model_syms);
}

unsigned tiku_model_sym_count(void)
{
    unsigned i, n = 0;
    for (i = 0; i < TIKU_MODEL_SYM_MAX; i++) {
        if (model_syms[i].used) {
            n++;
        }
    }
    return n;
}

/** @brief Look a name up.  Returns 1 and fills @p out, or 0 if not registered. */
static int model_sym_lookup(const char *name, uintptr_t *out)
{
    unsigned i;
    for (i = 0; i < TIKU_MODEL_SYM_MAX; i++) {
        if (model_syms[i].used && strcmp(model_syms[i].name, name) == 0) {
            *out = model_syms[i].addr;
            return 1;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Little-endian u32 read that assumes nothing about alignment. */
static uint32_t model_rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

/** @brief Little-endian u32 write, likewise. */
static void model_wr32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, sizeof v);
}

/**
 * @brief The @p i'th name in a NUL-separated table, or NULL if it runs out.
 *
 * The table is a file region, so it may be truncated or missing its terminator;
 * this never reads past @p end.
 */
static const char *model_sym_nth(const char *tab, const char *end, uint32_t i)
{
    const char *p = tab;
    uint32_t k;

    for (k = 0; k < i; k++) {
        while (p < end && *p != '\0') {
            p++;
        }
        if (p >= end) {
            return NULL;                    /* ran out before reaching i */
        }
        p++;                                /* step over the NUL */
    }
    if (p >= end) {
        return NULL;
    }
    /* The name itself must terminate inside the region. */
    {
        const char *q = p;
        while (q < end && *q != '\0') {
            q++;
        }
        if (q >= end) {
            return NULL;                    /* unterminated */
        }
    }
    return p;
}

/*---------------------------------------------------------------------------*/
/* OPEN                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Validate a RELOC header and fill @p out.
 *
 * Every bound is written as a SUBTRACTION against the remaining length rather
 * than an addition compared to it.  Additions of two file-supplied u32s wrap --
 * on a 16-bit `unsigned` that is a live out-of-bounds hazard, and this file
 * builds for MSP430 too -- and a wrapped sum passes a naive check.
 */
static int model_open_reloc(tiku_model_t *out)
{
    const uint8_t *b = out->base;
    size_t         n = out->len;
    uint32_t ver, hdrlen, woff, wlen, coff, clen, roff, nsites, soff, nsyms;
    uint32_t crc_w, crc_c, crc_t;
    uint32_t i;

    if (n < AXM_HDR_BYTES) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    ver    = model_rd32(b + AXM_W_VERSION * 4u);
    hdrlen = model_rd32(b + AXM_W_HDRLEN  * 4u);
    woff   = model_rd32(b + AXM_W_WOFF    * 4u);
    wlen   = model_rd32(b + AXM_W_WLEN    * 4u);
    coff   = model_rd32(b + AXM_W_COFF    * 4u);
    clen   = model_rd32(b + AXM_W_CLEN    * 4u);
    roff   = model_rd32(b + AXM_W_ROFF    * 4u);
    nsites = model_rd32(b + AXM_W_NSITES  * 4u);
    soff   = model_rd32(b + AXM_W_SOFF    * 4u);
    nsyms  = model_rd32(b + AXM_W_NSYMS   * 4u);
    crc_w  = model_rd32(b + AXM_W_CRC_W   * 4u);
    crc_c  = model_rd32(b + AXM_W_CRC_C   * 4u);
    crc_t  = model_rd32(b + AXM_W_CRC_T   * 4u);

    if (ver != AXM_VERSION || hdrlen != AXM_HDR_BYTES) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    /* Sections in order, each inside the file, none overlapping its neighbour.
     * The packer emits exactly this order and pads for alignment, so anything
     * else is a file this loader did not produce. */
    if (woff != AXM_HDR_BYTES ||
        wlen > (uint32_t)n - woff ||
        coff < woff + wlen || coff > (uint32_t)n ||
        clen > (uint32_t)n - coff ||
        roff < coff + clen || roff > (uint32_t)n ||
        nsites > ((uint32_t)n - roff) / AXM_SITE_BYTES ||
        soff != roff + nsites * AXM_SITE_BYTES || soff > (uint32_t)n) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    /* A relocatable model needs weights to relocate against, a buffer to patch,
     * and at least symbol 0 (the weights themselves). */
    if (wlen == 0u || clen == 0u || nsyms == 0u) {
        return TIKU_MODEL_ERR_FORMAT;
    }

    if (tiku_nvm_crc32(b + woff, wlen) != crc_w ||
        tiku_nvm_crc32(b + coff, clen) != crc_c ||
        tiku_nvm_crc32(b + roff, n - roff) != crc_t) {
        return TIKU_MODEL_ERR_CRC;
    }

    out->fmt         = (uint8_t)TIKU_MODEL_FMT_RELOC;
    out->weights     = b + woff;
    out->weights_len = wlen;
    out->cmd         = b + coff;
    out->cmd_len     = clen;
    out->sites       = b + roff;
    out->nsites      = nsites;
    out->syms        = (const char *)(b + soff);
    out->nsyms       = nsyms;

    /* Walk the WHOLE site table now, so nothing downstream has to re-check it:
     * a site must be a 4-byte word wholly inside the command buffer, and must
     * name a symbol index that exists.  This is the check that keeps a corrupt
     * table from turning into a write outside the buffer. */
    for (i = 0; i < nsites; i++) {
        const uint8_t *e = out->sites + (size_t)i * AXM_SITE_BYTES;
        uint32_t off = model_rd32(e);
        uint32_t sym = model_rd32(e + 4u);

        if ((off & 3u) != 0u || off > (uint32_t)clen - 4u || clen < 4u) {
            return TIKU_MODEL_ERR_FORMAT;
        }
        if (sym >= nsyms) {
            return TIKU_MODEL_ERR_FORMAT;
        }
    }
    /* Every name must be present and terminated inside the symbol region --
     * checked here rather than at resolve time, so prepare() cannot walk off
     * the end of a truncated table. */
    for (i = 0; i < nsyms; i++) {
        if (model_sym_nth(out->syms, (const char *)(b + n), i) == NULL) {
            return TIKU_MODEL_ERR_FORMAT;
        }
    }
    return TIKU_MODEL_OK;
}

int tiku_model_open(tiku_tfs_t *fs, const char *name, tiku_model_t *out)
{
    const void *p = NULL;
    size_t      n = 0u;

    if (fs == NULL || name == NULL || out == NULL) {
        return TIKU_MODEL_ERR_PARAM;
    }
    memset(out, 0, sizeof *out);
    if (tiku_tfs_map(fs, name, &p, &n) != TFS_OK) {
        return TIKU_MODEL_ERR_NOENT;
    }
    out->base = (const uint8_t *)p;
    out->len  = n;

    /*
     * Format is sniffed, not configured: a file carrying the packer's magic is
     * relocatable, anything else is opaque bytes.  That keeps "provision a
     * palette" and "provision a compiled model" the same operation.
     *
     * The magic is tested as soon as there are four bytes to test, NOT once
     * there is a whole header.  Requiring a header first meant a model whose
     * transfer was cut short -- magic present, 40 bytes long -- fell through to
     * RAW and was handed back as a perfectly good opaque 40-byte model.  The
     * magic is a statement of intent, so once it is there the file must BE a
     * valid relocatable model or be refused; below four bytes there is nothing
     * to distinguish a truncated model from a genuinely tiny payload.
     */
    if (n >= 4u && model_rd32(out->base + AXM_W_MAGIC * 4u) == AXM_MAGIC) {
        return model_open_reloc(out);
    }

    if (n == 0u) {
        return TIKU_MODEL_ERR_FORMAT;       /* an empty file is not a model */
    }
    out->fmt         = (uint8_t)TIKU_MODEL_FMT_RAW;
    out->weights     = out->base;
    out->weights_len = n;
    return TIKU_MODEL_OK;
}

/*---------------------------------------------------------------------------*/
/* PREPARE                                                                   */
/*---------------------------------------------------------------------------*/

int tiku_model_prepare(const tiku_model_t *m, void *dst, size_t cap,
                       size_t *out_len, const char **bad_sym)
{
    uint8_t  *d;
    uintptr_t resolved[TIKU_MODEL_SYM_MAX];
    uint32_t  i;

    if (m == NULL || m->base == NULL) {
        return TIKU_MODEL_ERR_PARAM;
    }
    if (bad_sym != NULL) {
        *bad_sym = NULL;
    }
    if (m->fmt == (uint8_t)TIKU_MODEL_FMT_RAW) {
        /* Nothing is baked in, so nothing needs correcting -- the caller reads
         * the mapped bytes directly. */
        if (out_len != NULL) {
            *out_len = 0u;
        }
        return TIKU_MODEL_OK;
    }
    if (dst == NULL) {
        return TIKU_MODEL_ERR_PARAM;
    }
    if (cap < m->cmd_len) {
        return TIKU_MODEL_ERR_SPACE;
    }
    /* More symbols than the registry can hold cannot be resolved, and finding
     * that out halfway through a patched buffer would leave it half-corrected. */
    if (m->nsyms > TIKU_MODEL_SYM_MAX) {
        return TIKU_MODEL_ERR_FULL;
    }

    /*
     * Resolve EVERY symbol before patching anything.  A model naming one
     * unknown symbol must fail with the command buffer untouched, not with
     * some sites corrected and the rest still holding bare addends -- that
     * second state runs and produces wrong answers.
     *
     * Index 0 is always the weights, wherever the store mapped them; the
     * loader knows that without being told, which is why the registry never
     * carries a per-model entry.
     */
    for (i = 0; i < m->nsyms; i++) {
        if (i == 0u) {
            resolved[0] = (uintptr_t)m->weights;
            continue;
        }
        {
            const char *nm = model_sym_nth(m->syms,
                                           (const char *)(m->base + m->len), i);
            if (nm == NULL) {                /* open() proved otherwise */
                return TIKU_MODEL_ERR_FORMAT;
            }
            if (!model_sym_lookup(nm, &resolved[i])) {
                if (bad_sym != NULL) {
                    *bad_sym = nm;           /* say WHICH symbol */
                }
                return TIKU_MODEL_ERR_SYMBOL;
            }
        }
    }

    /* Copy the command buffer out of NVM, then correct it in place.  The
     * WEIGHTS are not copied: they stay mapped, in the same NVM the engine read
     * them from when they were part of .rodata. */
    d = (uint8_t *)dst;
    memcpy(d, m->cmd, m->cmd_len);

    for (i = 0; i < m->nsites; i++) {
        const uint8_t *e = m->sites + (size_t)i * AXM_SITE_BYTES;
        uint32_t off = model_rd32(e);
        uint32_t sym = model_rd32(e + 4u);
        /* ARM REL: the addend is already at the site, so the fix is one add. */
        uint32_t addend = model_rd32(d + off);

        model_wr32(d + off, (uint32_t)(resolved[sym] + addend));
    }

    if (out_len != NULL) {
        *out_len = m->cmd_len;
    }
    return TIKU_MODEL_OK;
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS                                                               */
/*---------------------------------------------------------------------------*/

const char *tiku_model_strerror(int status)
{
    switch (status) {
    case TIKU_MODEL_OK:         return "ok";
    case TIKU_MODEL_ERR_PARAM:  return "bad argument";
    case TIKU_MODEL_ERR_NOENT:  return "no such model file";
    case TIKU_MODEL_ERR_FORMAT: return "malformed model file";
    case TIKU_MODEL_ERR_CRC:    return "model file failed its checksum";
    case TIKU_MODEL_ERR_SPACE:  return "buffer too small for the command stream";
    case TIKU_MODEL_ERR_SYMBOL: return "unresolved symbol";
    case TIKU_MODEL_ERR_FULL:   return "symbol registry full";
    default:                    return "unknown model status";
    }
}
