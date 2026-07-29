/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.c - Tiku model loader.  See tiku_model.h for the interface.
 *
 * Validates a packed model file, then patches its relocation sites in place.
 * The file is a 96-byte little-endian header naming each section's offset and
 * length, followed by the blobs, the site table and the symbol names.
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
 * Version 2 added the third CRC, over the relocation table and symbol names.
 *
 * v1 covered the weights and the command buffer but left the TABLE unchecked --
 * and the table is the one part of the file that says "write four bytes here".
 * Structural validation bounds every entry, so a corrupt table cannot escape
 * its section; but an in-bounds corrupt OFFSET silently patches the wrong word,
 * and the symptom is wrong inference rather than a fault.  A checksum is the
 * only thing that catches that.
 *
 * Version 3 adds the descriptor, the labels and the string pool (see the file
 * header).  Older versions are REFUSED rather than read with a gap: a v2 file
 * carries no descriptor, so a model-free image loading one would run the engine
 * against whatever the descriptor memory happened to hold.  There is no
 * deployed v2 file to be compatible with.
 */
#define AXM_VERSION      3u
#define AXM_HDR_BYTES    96u
#define AXM_SITE_BYTES   8u            /* u16 sect + u16 sym + u32 offset */

/* Header word indices (byte offset = index * 4). */
#define AXM_W_MAGIC      0u
#define AXM_W_VERSION    1u
#define AXM_W_HDRLEN     2u
#define AXM_W_WOFF       3u
#define AXM_W_WLEN       4u
#define AXM_W_COFF       5u
#define AXM_W_CLEN       6u
#define AXM_W_DOFF       7u
#define AXM_W_DLEN       8u
#define AXM_W_LOFF       9u
#define AXM_W_LLEN       10u
#define AXM_W_TOFF       11u
#define AXM_W_TLEN       12u
#define AXM_W_ROFF       13u
#define AXM_W_NSITES     14u
#define AXM_W_SOFF       15u
#define AXM_W_NSYMS      16u
#define AXM_W_POLEN      17u
#define AXM_W_NLABELS    18u
#define AXM_W_CRC_W      19u
#define AXM_W_CRC_C      20u
#define AXM_W_CRC_M      21u           /* desc + labels + strings */
#define AXM_W_CRC_T      22u

/* Relocation targets naming a part of the model itself.  The loader owns these
 * because only it knows where each part ended up; anything else -- including an
 * unrecognised '@' name -- falls through to the symbol registry. */
#define AXM_SYM_WEIGHTS  "@weights"
#define AXM_SYM_CMD      "@cmd"
#define AXM_SYM_DESC     "@desc"
#define AXM_SYM_LABELS   "@labels"
#define AXM_SYM_STRINGS  "@strings"

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

/** @brief Little-endian u16 read that assumes nothing about alignment. */
static uint16_t model_rd16(const uint8_t *p)
{
    uint16_t v;
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
 * Every bound is a subtraction against the remaining length, never an addition
 * compared to it: two file-supplied u32s added together wrap, and a wrapped sum
 * passes a naive check.
 */
static int model_open_reloc(tiku_model_t *out)
{
    const uint8_t *b = out->base;
    size_t         n = out->len;
    uint32_t ver, hdrlen, woff, wlen, coff, clen, roff, nsites, soff, nsyms;
    uint32_t doff, dlen, loff, llen, toff, tlen, polen, nlabels;
    uint32_t crc_w, crc_c, crc_m, crc_t;
    uint32_t i;

    if (n < AXM_HDR_BYTES) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    ver     = model_rd32(b + AXM_W_VERSION * 4u);
    hdrlen  = model_rd32(b + AXM_W_HDRLEN  * 4u);
    woff    = model_rd32(b + AXM_W_WOFF    * 4u);
    wlen    = model_rd32(b + AXM_W_WLEN    * 4u);
    coff    = model_rd32(b + AXM_W_COFF    * 4u);
    clen    = model_rd32(b + AXM_W_CLEN    * 4u);
    doff    = model_rd32(b + AXM_W_DOFF    * 4u);
    dlen    = model_rd32(b + AXM_W_DLEN    * 4u);
    loff    = model_rd32(b + AXM_W_LOFF    * 4u);
    llen    = model_rd32(b + AXM_W_LLEN    * 4u);
    toff    = model_rd32(b + AXM_W_TOFF    * 4u);
    tlen    = model_rd32(b + AXM_W_TLEN    * 4u);
    roff    = model_rd32(b + AXM_W_ROFF    * 4u);
    nsites  = model_rd32(b + AXM_W_NSITES  * 4u);
    soff    = model_rd32(b + AXM_W_SOFF    * 4u);
    nsyms   = model_rd32(b + AXM_W_NSYMS   * 4u);
    polen   = model_rd32(b + AXM_W_POLEN   * 4u);
    nlabels = model_rd32(b + AXM_W_NLABELS * 4u);
    crc_w   = model_rd32(b + AXM_W_CRC_W   * 4u);
    crc_c   = model_rd32(b + AXM_W_CRC_C   * 4u);
    crc_m   = model_rd32(b + AXM_W_CRC_M   * 4u);
    crc_t   = model_rd32(b + AXM_W_CRC_T   * 4u);

    if (ver != AXM_VERSION || hdrlen != AXM_HDR_BYTES) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    /* Sections in order, each inside the file, none overlapping its neighbour.
     * The packer emits exactly this order and pads for alignment, so anything
     * else is a file this loader did not produce.  Every bound is a
     * SUBTRACTION against the remaining length, never an addition compared to
     * it -- see the function comment. */
    if (woff != AXM_HDR_BYTES ||
        wlen > (uint32_t)n - woff ||
        coff < woff + wlen || coff > (uint32_t)n ||
        clen > (uint32_t)n - coff ||
        doff < coff + clen || doff > (uint32_t)n ||
        dlen > (uint32_t)n - doff ||
        loff < doff + dlen || loff > (uint32_t)n ||
        llen > (uint32_t)n - loff ||
        toff < loff + llen || toff > (uint32_t)n ||
        tlen > (uint32_t)n - toff ||
        roff < toff + tlen || roff > (uint32_t)n ||
        nsites > ((uint32_t)n - roff) / AXM_SITE_BYTES ||
        soff != roff + nsites * AXM_SITE_BYTES || soff > (uint32_t)n) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    /* A relocatable model needs weights to relocate against, a buffer to patch,
     * a descriptor to be run from, and at least one symbol. */
    if (wlen == 0u || clen == 0u || dlen == 0u || nsyms == 0u) {
        return TIKU_MODEL_ERR_FORMAT;
    }
    /* The label array is pointer-sized entries, and nlabels must agree with the
     * section it describes -- otherwise a caller building a label list from
     * nlabels would read past the section. */
    if ((llen & 3u) != 0u || nlabels != llen / 4u) {
        return TIKU_MODEL_ERR_FORMAT;
    }

    if (tiku_nvm_crc32(b + woff, wlen) != crc_w ||
        tiku_nvm_crc32(b + coff, clen) != crc_c ||
        tiku_nvm_crc32(b + doff, roff - doff) != crc_m ||
        tiku_nvm_crc32(b + roff, n - roff) != crc_t) {
        return TIKU_MODEL_ERR_CRC;
    }

    out->fmt            = (uint8_t)TIKU_MODEL_FMT_RELOC;
    out->weights        = b + woff;
    out->weights_len    = wlen;
    out->cmd            = b + coff;
    out->cmd_len        = clen;
    out->desc           = b + doff;
    out->desc_len       = dlen;
    out->labels         = (llen != 0u) ? b + loff : NULL;
    out->labels_len     = llen;
    out->nlabels        = nlabels;
    out->strings        = (tlen != 0u) ? (const char *)(b + toff) : NULL;
    out->strings_len    = tlen;
    out->packed_out_len = polen;
    out->sites          = b + roff;
    out->nsites         = nsites;
    out->syms           = (const char *)(b + soff);
    out->nsyms          = nsyms;

    /* Walk the WHOLE site table now, so nothing downstream has to re-check it:
     * a site must name a section the model actually has, must be a 4-byte word
     * wholly inside THAT section, and must name a symbol index that exists.
     * This is the check that keeps a corrupt table from turning into a write
     * outside the buffer it was supposed to patch. */
    for (i = 0; i < nsites; i++) {
        const uint8_t *e = out->sites + (size_t)i * AXM_SITE_BYTES;
        uint32_t sect = (uint32_t)model_rd16(e);
        uint32_t sym  = (uint32_t)model_rd16(e + 2u);
        uint32_t off  = model_rd32(e + 4u);
        uint32_t slen;

        switch (sect) {
        case (uint32_t)TIKU_MODEL_SECT_CMD:    slen = clen; break;
        case (uint32_t)TIKU_MODEL_SECT_DESC:   slen = dlen; break;
        case (uint32_t)TIKU_MODEL_SECT_LABELS: slen = llen; break;
        default:                               return TIKU_MODEL_ERR_FORMAT;
        }
        if (slen < 4u || (off & 3u) != 0u || off > slen - 4u) {
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

/** @brief The model's own bytes for a section, and how many. */
static const uint8_t *model_sect_src(const tiku_model_t *m, unsigned sect,
                                     size_t *len)
{
    switch (sect) {
    case TIKU_MODEL_SECT_CMD:    *len = m->cmd_len;    return m->cmd;
    case TIKU_MODEL_SECT_DESC:   *len = m->desc_len;   return m->desc;
    case TIKU_MODEL_SECT_LABELS: *len = m->labels_len; return m->labels;
    default:                     *len = 0u;            return NULL;
    }
}

/**
 * @brief Resolve a name the LOADER owns, i.e. a part of the model itself.
 *
 * @return 1 and sets @p out, or 0 if the name is not one of these (in which
 *         case it belongs to the registry).
 */
static int model_resolve_own(const tiku_model_t *m,
                             const tiku_model_dest_t *dst,
                             const char *nm, uintptr_t *out)
{
    if (strcmp(nm, AXM_SYM_WEIGHTS) == 0) {
        *out = (uintptr_t)m->weights;
    } else if (strcmp(nm, AXM_SYM_STRINGS) == 0) {
        *out = (uintptr_t)m->strings;
    } else if (strcmp(nm, AXM_SYM_CMD) == 0) {
        *out = (uintptr_t)dst[TIKU_MODEL_SECT_CMD].dst;
    } else if (strcmp(nm, AXM_SYM_DESC) == 0) {
        *out = (uintptr_t)dst[TIKU_MODEL_SECT_DESC].dst;
    } else if (strcmp(nm, AXM_SYM_LABELS) == 0) {
        *out = (uintptr_t)dst[TIKU_MODEL_SECT_LABELS].dst;
    } else {
        return 0;
    }
    return 1;
}

int tiku_model_prepare_all(const tiku_model_t *m, tiku_model_dest_t *dst,
                           const char **bad_sym)
{
    uintptr_t resolved[TIKU_MODEL_SYM_MAX];
    uint32_t  i;
    unsigned  s;

    if (m == NULL || m->base == NULL || dst == NULL) {
        return TIKU_MODEL_ERR_PARAM;
    }
    if (bad_sym != NULL) {
        *bad_sym = NULL;
    }
    if (m->fmt == (uint8_t)TIKU_MODEL_FMT_RAW) {
        /* Nothing is baked in, so nothing needs correcting -- the caller reads
         * the mapped bytes directly. */
        return TIKU_MODEL_OK;
    }
    if (m->nsyms > TIKU_MODEL_SYM_MAX) {
        /* More symbols than the registry can hold cannot be resolved, and
         * finding that out halfway through would leave a half-patched model. */
        return TIKU_MODEL_ERR_FULL;
    }

    /* Room for every section the model HAS.  A section the model has but the
     * caller did not provide for is refused here, before anything is written:
     * the sites naming it could not be resolved, and a model patched
     * everywhere except one section runs and answers wrongly. */
    for (s = 0; s < TIKU_MODEL_SECT_COUNT; s++) {
        size_t need = 0u;
        (void)model_sect_src(m, s, &need);
        if (need == 0u) {
            continue;
        }
        if (dst[s].dst == NULL) {
            return TIKU_MODEL_ERR_PARAM;
        }
        if (dst[s].cap < need) {
            return TIKU_MODEL_ERR_SPACE;
        }
    }

    /*
     * Resolve EVERY symbol before patching anything.  A model naming one
     * unknown symbol must fail with every section untouched, not with some
     * sites corrected and the rest still holding bare addends -- that second
     * state runs and produces wrong answers.
     *
     * Resolution is BY NAME throughout, including the model's own sections.
     * An earlier version reserved index 0 for the weights; that was a
     * positional rule of exactly the kind this loader refuses everywhere else,
     * and dropping it costs one string compare.
     */
    for (i = 0; i < m->nsyms; i++) {
        const char *nm = model_sym_nth(m->syms,
                                       (const char *)(m->base + m->len), i);
        if (nm == NULL) {                    /* open() proved otherwise */
            return TIKU_MODEL_ERR_FORMAT;
        }
        if (model_resolve_own(m, dst, nm, &resolved[i])) {
            continue;
        }
        if (!model_sym_lookup(nm, &resolved[i])) {
            if (bad_sym != NULL) {
                *bad_sym = nm;               /* say WHICH symbol */
            }
            return TIKU_MODEL_ERR_SYMBOL;
        }
    }

    /* Copy each section out of NVM, then correct it in place.  The WEIGHTS and
     * the STRING POOL are not copied: they stay mapped, in the same NVM the
     * engine read them from when they were part of .rodata. */
    for (s = 0; s < TIKU_MODEL_SECT_COUNT; s++) {
        size_t         len = 0u;
        const uint8_t *src = model_sect_src(m, s, &len);

        if (len != 0u) {
            memcpy(dst[s].dst, src, len);
        }
    }

    for (i = 0; i < m->nsites; i++) {
        const uint8_t *e = m->sites + (size_t)i * AXM_SITE_BYTES;
        uint32_t sect = (uint32_t)model_rd16(e);
        uint32_t sym  = (uint32_t)model_rd16(e + 2u);
        uint32_t off  = model_rd32(e + 4u);
        uint8_t *d    = (uint8_t *)dst[sect].dst;
        /* ARM REL: the addend is already at the site, so the fix is one add. */
        uint32_t addend = model_rd32(d + off);

        model_wr32(d + off, (uint32_t)(resolved[sym] + addend));
    }
    return TIKU_MODEL_OK;
}

int tiku_model_prepare(const tiku_model_t *m, void *dst, size_t cap,
                       size_t *out_len, const char **bad_sym)
{
    tiku_model_dest_t d[TIKU_MODEL_SECT_COUNT];
    int rc;

    if (m == NULL) {
        return TIKU_MODEL_ERR_PARAM;
    }
    memset(d, 0, sizeof d);
    d[TIKU_MODEL_SECT_CMD].dst = dst;
    d[TIKU_MODEL_SECT_CMD].cap = cap;

    /* The single-section convenience form.  It CANNOT serve a model that also
     * carries a descriptor, because prepare_all() would refuse the missing
     * destinations -- which is the intended outcome: silently building the
     * command buffer and leaving the descriptor unrelocated is precisely the
     * half-patched state the rest of this file is written to prevent. */
    rc = tiku_model_prepare_all(m, d, bad_sym);
    if (rc == TIKU_MODEL_OK && out_len != NULL) {
        *out_len = (m->fmt == (uint8_t)TIKU_MODEL_FMT_RAW) ? 0u : m->cmd_len;
    }
    return rc;
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
