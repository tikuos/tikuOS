/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crypto_arch.c - nRF54L15 CRACEN CryptoMaster offload backend.
 *
 * The CryptoMaster is a Silex Insight DMA front-end to fused symmetric
 * engines (BA411E AES, BA413 hash).  An operation is a chain of FETCH
 * descriptors -- configuration words routed to the selected engine's
 * config interface, then data routed to its data interface -- plus one
 * PUSH descriptor for the result:
 *
 *   desc = { addr, next, length, tag }
 *     next  = 0x1 terminates the chain
 *     length bit 29 = realign to the FIFO word boundary after this block
 *     tag   = engine id | 0x10 (config iface) | 0x20 ("last" strobe)
 *             | datatype << 6 | (config-register offset << 8)
 *
 * Engine ids: AES = 0x1, hash = 0x3, bypass = 0xF (from the BSD-licensed
 * nrfx HAL, which documents the wire format; this implementation is our
 * own).  The engine is enabled for exactly the duration of one operation
 * (CRACEN.ENABLE is read-modify-write shared with the TRNG's RNG bit and
 * gated off after, same discipline as tiku_trng_arch.c), then the DMA is
 * soft-reset so no state leaks between operations.
 *
 * The BA413 hash-engine CONFIG word for SHA-256 was determined EMPIRICALLY
 * on this die against known SHA-256 vectors via the cryptoprobe shell
 * command (mode field one-hot, hardware padding enabled) -- see the
 * bring-up notes in kintsugi/nrf54l15_phase6_coproc_radio_cracen.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_crypto_arch.h>
#include <arch/nordic/tiku_trng_arch.h>       /* seed source for the masker  */
#include <arch/nordic/tiku_device_select.h>   /* NRF_CRACEN_S / NRF_CRACENCORE_S */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Wire format                                                               */
/*---------------------------------------------------------------------------*/

typedef struct __attribute__((packed, aligned(4))) cm_desc {
    const uint8_t   *addr;
    struct cm_desc  *next;
    uint32_t         length;
    uint32_t         tag;
} cm_desc_t;

#define CM_DESC_STOP        ((cm_desc_t *)0x1)
#define CM_LEN_REALIGN      (1UL << 29)

#define CM_TAG_ENGINE_AES   0x1UL
#define CM_TAG_ENGINE_HASH  0x3UL
#define CM_TAG_CONFIG       0x10UL
#define CM_TAG_LAST         0x20UL
#define CM_TAG_DATATYPE(n)  ((uint32_t)(n) << 6)
#define CM_TAG_CFGREG(off)  ((uint32_t)(off) << 8)

/* BA413 hash CONFIG word: SHA-256 mode (one-hot 0x08) | hardware padding
 * (bit 9) | final digest (bit 10).  Semantics from the vendor driver's
 * documented encoding; verified on this die against FIPS 180-4 vectors
 * via the cryptoprobe command. */
#define CM_HASH_CFG_SHA256  0x00000608UL

/* CRACEN.ENABLE bit for the CryptoMaster (wrapper gate). */
#define CRACEN_EN_CRYPTOMASTER \
    (CRACEN_ENABLE_CRYPTOMASTER_Msk | CRACEN_ENABLE_PKEIKG_Msk | \
     CRACEN_ENABLE_RNG_Msk)

/*---------------------------------------------------------------------------*/
/* Mode + counters                                                           */
/*---------------------------------------------------------------------------*/

static uint8_t  s_mode = TIKU_CRYPTO_HW_MODE_AUTO;
static uint16_t s_hw_ops, s_sw_ops, s_hw_errs;

uint8_t tiku_crypto_hw_mode(void)            { return s_mode; }
void    tiku_crypto_hw_mode_set(uint8_t m)   { s_mode = (m != 0u) ? 1u : 0u; }
void    tiku_crypto_hw_count_sw(void)
{
    if (s_sw_ops != 0xFFFFu) { s_sw_ops++; }
}
void tiku_crypto_hw_counters(uint16_t *hw_ops, uint16_t *sw_ops,
                             uint16_t *hw_errs)
{
    if (hw_ops)  { *hw_ops  = s_hw_ops;  }
    if (sw_ops)  { *sw_ops  = s_sw_ops;  }
    if (hw_errs) { *hw_errs = s_hw_errs; }
}

/*---------------------------------------------------------------------------*/
/* CryptoMaster run                                                          */
/*---------------------------------------------------------------------------*/

/* Fetch/push error and busy conditions (MDK bit positions). */
#define CM_INT_ERRORS \
    (CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_FETCHERERROR_Msk | \
     CRACENCORE_CRYPTMSTRDMA_INTSTATRAW_PUSHERERROR_Msk)
#define CM_STATUS_BUSY \
    (CRACENCORE_CRYPTMSTRDMA_STATUS_FETCHBUSY_Msk | \
     CRACENCORE_CRYPTMSTRDMA_STATUS_PUSHBUSY_Msk)

/**
 * @brief Run one CryptoMaster operation and wait for completion.
 *
 * Enables the engine, points the fetcher/pusher at the descriptor chains
 * (indirect mode), starts both, spins until idle (the engine is far faster
 * than an interrupt round-trip -- the nrfx driver documents the same
 * choice), then soft-resets the DMA and gates the engine back off.
 *
 * @return 0 on success, -1 on a fetch/push error or spin timeout.
 */
/**
 * @brief One-time masking-seed load: the CryptoMaster (and IKG) refuse to
 * run until CRACEN.SEED[0..11] is written and SEEDVALID set (observed
 * on-die as an instant fetcher error with zero bytes moved).  Seed from
 * the CRACEN TRNG -- the masker wants real entropy, and we have the
 * proven driver for it.
 *
 * @return 0 once the seed is valid, -1 if the TRNG failed.
 */
static int cm_ensure_seed(void)
{
    uint32_t seed[12];
    uint8_t  i;

    if (NRF_CRACEN_S->SEEDVALID != 0UL) {
        return 0;                        /* already seeded this power cycle */
    }
    if (tiku_trng_arch_read_bytes((uint8_t *)seed, sizeof seed)
        != TIKU_TRNG_OK) {
        return -1;
    }
    for (i = 0; i < 12u; i++) {
        NRF_CRACEN_S->SEED[i] = seed[i];
    }
    NRF_CRACEN_S->SEEDVALID = 1UL;
    memset(seed, 0, sizeof seed);        /* wipe the raw entropy */
    return 0;
}

static uint32_t s_dbg_ints, s_dbg_status, s_dbg_stage;
static uint8_t  s_mask_loaded;

static int cm_run_raw(cm_desc_t *fetch, cm_desc_t *push);

/**
 * @brief One-time countermeasure-mask load.
 *
 * The CryptoMaster's data path runs through a DPA masking network that
 * stays DARK until a random mask word is DMA-written to the AES engine's
 * config offset 0x68 -- config-interface writes work unmasked, data
 * through any engine (even the bypass) never emerges.  Observed on-die
 * exactly so: fetcher completes, pusher starves.  Load one TRNG word,
 * output descriptor null (config-only ops produce nothing).
 */
static int cm_ensure_mask(void)
{
    static cm_desc_t fetch, push;
    static uint32_t  mask_word;
    int rc;

    if (s_mask_loaded) {
        return 0;
    }
    if (tiku_trng_arch_read_bytes((uint8_t *)&mask_word,
                                  sizeof mask_word) != TIKU_TRNG_OK) {
        return -1;
    }
    fetch.addr   = (const uint8_t *)&mask_word;
    fetch.length = sizeof mask_word | CM_LEN_REALIGN;
    fetch.tag    = CM_TAG_ENGINE_AES | CM_TAG_CONFIG | CM_TAG_CFGREG(0x68);
    fetch.next   = CM_DESC_STOP;

    push.addr   = (const uint8_t *)0;
    push.length = 0UL;
    push.tag    = 0xFUL;
    push.next   = CM_DESC_STOP;

    rc = cm_run_raw(&fetch, &push);
    if (rc == 0) {
        s_mask_loaded = 1u;
        mask_word = 0UL;                 /* wipe */
    }
    return rc;
}

static int cm_run(cm_desc_t *fetch, cm_desc_t *push)
{
    s_dbg_stage = 1;
    if (cm_ensure_seed() != 0) {
        return -1;
    }
    if (cm_ensure_mask() != 0) {
        return -1;
    }
    s_dbg_stage = 2;
    return cm_run_raw(fetch, push);
}

static int cm_run_raw(cm_desc_t *fetch, cm_desc_t *push)
{
    uint32_t spin;
    int      rc = -1;
    NRF_CRACEN_S->ENABLE |= CRACEN_EN_CRYPTOMASTER;

    NRF_CRACENCORE_S->CRYPTMSTRDMA.INTSTATCLR   = 0xFFFFFFFFUL;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.FETCHADDRLSB = (uint32_t)fetch;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.PUSHADDRLSB  = (uint32_t)push;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG =
        CRACENCORE_CRYPTMSTRDMA_CONFIG_FETCHCTRLINDIRECT_Msk |
        CRACENCORE_CRYPTMSTRDMA_CONFIG_PUSHCTRLINDIRECT_Msk;

    __asm__ volatile ("dmb" ::: "memory");   /* descriptors visible first */

    NRF_CRACENCORE_S->CRYPTMSTRDMA.START =
        CRACENCORE_CRYPTMSTRDMA_START_STARTFETCH_Msk |
        CRACENCORE_CRYPTMSTRDMA_START_STARTPUSH_Msk;

    /* Sub-ms hardware; generous bound so a wedged engine cannot hang the
     * cooperative loop (fail -> caller falls back to software). */
    for (spin = 0; spin < 2000000UL; spin++) {
        uint32_t ints = NRF_CRACENCORE_S->CRYPTMSTRDMA.INTSTATRAW;
        s_dbg_ints   = ints;
        s_dbg_status = NRF_CRACENCORE_S->CRYPTMSTRDMA.STATUS;
        if (ints & CM_INT_ERRORS) {
            rc = -1;
            break;
        }
        if ((s_dbg_status & CM_STATUS_BUSY) == 0UL) {
            rc = 0;
            break;
        }
    }
    s_dbg_stage = 3;

    /* Soft-reset the DMA (self-clearing pulse) and gate the engine off. */
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG =
        CRACENCORE_CRYPTMSTRDMA_CONFIG_SOFTRST_Msk;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG = 0UL;
    NRF_CRACEN_S->ENABLE &= ~CRACEN_EN_CRYPTOMASTER;

    return rc;
}

/*---------------------------------------------------------------------------*/
/* Hash (BA413)                                                              */
/*---------------------------------------------------------------------------*/

/* The CryptoMaster DMA, like every Nordic DMA, fetches from RAM only --
 * a message living in RRAM (rodata, or a cert parsed in place) faults the
 * fetcher instantly (verified on-die: FETCHERERROR with zero bytes moved).
 * One-shot inputs are therefore staged through this SRAM bounce buffer;
 * anything larger falls back to the software path until the streaming
 * (context-save) interface lands. */
#define CM_STAGE_MAX 4096u
static uint8_t cm_stage[CM_STAGE_MAX] __attribute__((aligned(4)));

static int hash_run(uint32_t cfg, const void *msg, size_t len,
                    uint8_t *out, size_t outlen)
{
    /* Descriptors + config word must live in RAM the DMA can fetch. */
    static cm_desc_t fetch[2];
    static cm_desc_t push;
    static uint32_t  cfg_word;

    if (len > CM_STAGE_MAX) {
        return -2;                      /* too big for the stage: use sw */
    }
    memcpy(cm_stage, msg, len);
    cfg_word = cfg;

    fetch[0].addr   = (const uint8_t *)&cfg_word;
    fetch[0].length = sizeof cfg_word | CM_LEN_REALIGN;
    fetch[0].tag    = CM_TAG_ENGINE_HASH | CM_TAG_CONFIG | CM_TAG_CFGREG(0);
    fetch[0].next   = &fetch[1];

    /* Arbitrary byte lengths: REALIGN pads the FIFO transfer to a word
     * multiple, and the tag's invalid-bytes field (bits 15:8) tells the
     * engine how many trailing pad bytes to IGNORE.  Without the field the
     * dummies get hashed (wrong digest); without REALIGN an unaligned
     * length faults the fetcher -- both observed on-die. */
    fetch[1].addr   = cm_stage;
    fetch[1].length = (uint32_t)len | CM_LEN_REALIGN;
    fetch[1].tag    = CM_TAG_ENGINE_HASH | CM_TAG_LAST | CM_TAG_DATATYPE(0) |
                      (((4u - ((uint32_t)len & 3u)) & 3u) << 8);
    fetch[1].next   = CM_DESC_STOP;

    push.addr   = out;
    push.length = (uint32_t)outlen | CM_LEN_REALIGN;
    push.tag    = CM_TAG_LAST;
    push.next   = CM_DESC_STOP;

    return cm_run(fetch, &push);
}

int tiku_crypto_arch_sha256(const void *msg, size_t len, uint8_t out[32])
{
    int rc = hash_run(CM_HASH_CFG_SHA256, msg, len, out, 32u);

    if (rc == 0) {
        if (s_hw_ops != 0xFFFFu) { s_hw_ops++; }
    } else if (s_hw_errs != 0xFFFFu) {
        s_hw_errs++;
    }
    return rc;
}

/*---------------------------------------------------------------------------*/
/* AES-GCM (BA411E)                                                          */
/*---------------------------------------------------------------------------*/

/* BA411 config word: one-hot mode in bits [16:8] (GCM = 0x040 = mode-id 6),
 * decrypt bit 0; key/IV loaded through the config interface (offsets 0x8 and
 * 0x28); AAD is engine datatype 1, payload datatype 0; the final input block
 * is the 16-byte big-endian lenA||lenC pair, then the 16-byte tag follows
 * the payload on the push side.  Semantics from the vendor driver's
 * documented flow; verified on-die against the software GCM (NIST-vector
 * proven) -- see cryptoprobe gcm. */
#define CM_AES_CFG_GCM      (0x040UL << 8)
#define CM_AES_CFG_DECRYPT  0x1UL
#define CM_AES_REG_KEY      0x08u
#define CM_AES_REG_IV       0x28u

/* True when the DMA can fetch @p p directly (on-die SRAM). */
static int cm_src_in_ram(const void *p)
{
    return ((uintptr_t)p >> 24) == 0x20u;
}

int tiku_crypto_arch_aes_gcm(int decrypt, uint32_t cfg_extra,
                             const uint8_t *key, size_t key_sz,
                             const uint8_t iv[12],
                             const uint8_t *aad, size_t aad_sz,
                             const uint8_t *in, size_t in_sz,
                             uint8_t *out, uint8_t tag[16])
{
    static cm_desc_t fetch[6];
    static cm_desc_t push[3];
    static uint32_t  cfg_word;
    static uint8_t   keybuf[32] __attribute__((aligned(4)));
    static uint8_t   ivbuf[12]  __attribute__((aligned(4)));
    static uint8_t   lenblk[16] __attribute__((aligned(4)));
    uint64_t bits;
    cm_desc_t *d = fetch;
    int i;

    if (key_sz != 16u && key_sz != 32u) {
        return -2;
    }
    /* Inputs the DMA cannot reach (RRAM) would need staging; the TLS/kit
     * callers hand SRAM buffers, so keep the fast path simple. */
    if ((aad_sz && !cm_src_in_ram(aad)) || (in_sz && !cm_src_in_ram(in))) {
        return -2;
    }

    cfg_word = CM_AES_CFG_GCM | cfg_extra | (decrypt ? CM_AES_CFG_DECRYPT : 0);
    memcpy(keybuf, key, key_sz);
    memcpy(ivbuf, iv, 12u);

    /* lenA || lenC, both in BITS, big-endian. */
    bits = (uint64_t)aad_sz * 8u;
    for (i = 0; i < 8; i++) {
        lenblk[i]     = (uint8_t)(bits >> (56 - 8 * i));
    }
    bits = (uint64_t)in_sz * 8u;
    for (i = 0; i < 8; i++) {
        lenblk[8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }

    d->addr = (const uint8_t *)&cfg_word;
    d->length = 4u | CM_LEN_REALIGN;
    d->tag = CM_TAG_ENGINE_AES | CM_TAG_CONFIG | CM_TAG_CFGREG(0);
    d->next = d + 1; d++;

    d->addr = keybuf;
    d->length = (uint32_t)key_sz | CM_LEN_REALIGN;
    d->tag = CM_TAG_ENGINE_AES | CM_TAG_CONFIG | CM_TAG_CFGREG(CM_AES_REG_KEY);
    d->next = d + 1; d++;

    d->addr = ivbuf;
    d->length = 12u | CM_LEN_REALIGN;
    d->tag = CM_TAG_ENGINE_AES | CM_TAG_CONFIG | CM_TAG_CFGREG(CM_AES_REG_IV);
    d->next = d + 1; d++;

    /* AEAD data moves in 16-byte blocks: descriptor lengths align to 16
     * with the tag's invalid-bytes field covering the pad (the vendor flow
     * uses the same 0xf alignment mask).  Callers guarantee align-16
     * readable headroom past aad/in. */
    if (aad_sz) {
        uint32_t asz = ((uint32_t)aad_sz + 15u) & ~15u;
        d->addr = aad;
        d->length = asz | CM_LEN_REALIGN;
        d->tag = CM_TAG_ENGINE_AES | CM_TAG_DATATYPE(1) |
                 ((asz - (uint32_t)aad_sz) << 8);
        d->next = d + 1; d++;
    }
    if (in_sz) {
        uint32_t asz = ((uint32_t)in_sz + 15u) & ~15u;
        d->addr = in;
        d->length = asz | CM_LEN_REALIGN;
        d->tag = CM_TAG_ENGINE_AES | CM_TAG_DATATYPE(0) |
                 ((asz - (uint32_t)in_sz) << 8);
        d->next = d + 1; d++;
    }

    d->addr = lenblk;
    d->length = 16u | CM_LEN_REALIGN;
    d->tag = CM_TAG_ENGINE_AES | CM_TAG_DATATYPE(0) | CM_TAG_LAST;
    d->next = CM_DESC_STOP;

    /* Output stream: the engine EMITS the AAD first (authenticated
     * passthrough -- observed on-die: the first output bytes were the AAD),
     * then ciphertext/plaintext, then the 16-byte tag.  The AAD block is
     * routed to a discard sink; out gets align-16 REALIGN padding (caller
     * guarantees headroom). */
    {
        static uint8_t sink[64] __attribute__((aligned(4)));
        cm_desc_t *q = push;
        uint32_t aad_asz = ((uint32_t)aad_sz + 15u) & ~15u;

        if (aad_sz) {
            if (aad_asz > sizeof sink) {
                return -2;               /* oversized AAD: software path */
            }
            q->addr = sink;
            q->length = aad_asz | CM_LEN_REALIGN;
            q->tag = 0;
            q->next = q + 1; q++;
        }
        if (in_sz) {
            q->addr = out;
            q->length = (((uint32_t)in_sz + 15u) & ~15u) | CM_LEN_REALIGN;
            q->tag = 0;
            q->next = q + 1; q++;
        }
        q->addr = tag;
        q->length = 16u | CM_LEN_REALIGN;
        q->tag = CM_TAG_LAST;
        q->next = CM_DESC_STOP;
    }
    return cm_run(fetch, push);
}

/**
 * @brief Kit-safe AES-GCM: stages in/out through SRAM so the caller's
 * buffers need no alignment headroom and may live in RRAM.  Encrypt or
 * decrypt (verify) up to the stage size; larger -> -2 (software path).
 */
int tiku_crypto_arch_aes_gcm_kit(int decrypt,
                                 const uint8_t *key, size_t key_sz,
                                 const uint8_t iv[12],
                                 const uint8_t *aad, size_t aad_sz,
                                 const uint8_t *in, size_t in_sz,
                                 uint8_t *out, uint8_t tag[16])
{
    static uint8_t istage[CM_STAGE_MAX] __attribute__((aligned(16)));
    static uint8_t ostage[CM_STAGE_MAX] __attribute__((aligned(16)));
    static uint8_t astage[256]          __attribute__((aligned(16)));
    int rc;

    if (in_sz > CM_STAGE_MAX || aad_sz > sizeof astage) {
        return -2;
    }
    if (in_sz)  { memcpy(istage, in, in_sz); }
    if (aad_sz) { memcpy(astage, aad, aad_sz); }

    rc = tiku_crypto_arch_aes_gcm(decrypt, 0u, key, key_sz, iv,
                                  aad_sz ? astage : (const uint8_t *)0, aad_sz,
                                  in_sz  ? istage : (const uint8_t *)0, in_sz,
                                  ostage, tag);
    if (rc == 0 && in_sz) {
        memcpy(out, ostage, in_sz);
    }
    return rc;
}

/*---------------------------------------------------------------------------*/
/* Bring-up probes                                                           */
/*---------------------------------------------------------------------------*/

int tiku_crypto_arch_hash_probe(uint32_t cfg, const void *msg, size_t len,
                                uint8_t *out, size_t outlen)
{
    return hash_run(cfg, msg, len, out, outlen);
}

int tiku_crypto_arch_bypass_probe(const void *msg, size_t len, uint8_t *out)
{
    static cm_desc_t fetch, push;

    if (len > CM_STAGE_MAX) {
        return -2;
    }
    memcpy(cm_stage, msg, len);

    fetch.addr   = cm_stage;
    fetch.length = (uint32_t)len | CM_LEN_REALIGN;
    fetch.tag    = 0xFUL | CM_TAG_LAST;      /* bypass engine: pure DMA */
    fetch.next   = CM_DESC_STOP;

    push.addr   = out;
    push.length = (uint32_t)len | CM_LEN_REALIGN;
    push.tag    = CM_TAG_LAST;
    push.next   = CM_DESC_STOP;

    return cm_run(&fetch, &push);
}

uint32_t tiku_crypto_arch_hwcfg(uint8_t idx)
{
    volatile const uint32_t *hw =
        (volatile const uint32_t *)&NRF_CRACENCORE_S->CRYPTMSTRHW;
    uint32_t v;

    if (idx > 6u) {
        return 0xFFFFFFFFUL;
    }
    NRF_CRACEN_S->ENABLE |= CRACEN_EN_CRYPTOMASTER;
    v = hw[idx];
    NRF_CRACEN_S->ENABLE &= ~CRACEN_EN_CRYPTOMASTER;
    return v;
}

int tiku_crypto_arch_direct_probe(const void *msg, size_t len, uint8_t *out)
{
    uint32_t spin;
    int      rc = -1;

    if (len > CM_STAGE_MAX) {
        return -2;
    }
    if (cm_ensure_seed() != 0) {
        return -1;
    }
    memcpy(cm_stage, msg, len);

    NRF_CRACEN_S->ENABLE |= CRACEN_EN_CRYPTOMASTER;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.INTSTATCLR   = 0xFFFFFFFFUL;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG       = 0UL;   /* DIRECT mode */
    NRF_CRACENCORE_S->CRYPTMSTRDMA.FETCHADDRLSB = (uint32_t)cm_stage;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.FETCHLEN     = (uint32_t)len;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.FETCHTAG     = 0xFUL | CM_TAG_LAST;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.PUSHADDRLSB  = (uint32_t)out;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.PUSHLEN      = (uint32_t)len;
    __asm__ volatile ("dmb" ::: "memory");
    NRF_CRACENCORE_S->CRYPTMSTRDMA.START =
        CRACENCORE_CRYPTMSTRDMA_START_STARTFETCH_Msk |
        CRACENCORE_CRYPTMSTRDMA_START_STARTPUSH_Msk;

    for (spin = 0; spin < 2000000UL; spin++) {
        uint32_t ints = NRF_CRACENCORE_S->CRYPTMSTRDMA.INTSTATRAW;
        s_dbg_ints   = ints;
        s_dbg_status = NRF_CRACENCORE_S->CRYPTMSTRDMA.STATUS;
        if (ints & CM_INT_ERRORS) { rc = -1; break; }
        if ((s_dbg_status & CM_STATUS_BUSY) == 0UL) { rc = 0; break; }
    }
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG =
        CRACENCORE_CRYPTMSTRDMA_CONFIG_SOFTRST_Msk;
    NRF_CRACENCORE_S->CRYPTMSTRDMA.CONFIG = 0UL;
    NRF_CRACEN_S->ENABLE &= ~CRACEN_EN_CRYPTOMASTER;
    return rc;
}

void tiku_crypto_arch_dbg(uint32_t *ints, uint32_t *status,
                          uint32_t *seedvalid, uint32_t *stage)
{
    if (ints)      { *ints      = s_dbg_ints; }
    if (status)    { *status    = s_dbg_status; }
    if (seedvalid) { *seedvalid = NRF_CRACEN_S->SEEDVALID; }
    if (stage)     { *stage     = s_dbg_stage; }
}
