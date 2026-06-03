/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - RP2350 TRNG driver
 *
 * Hardware: RP2350 datasheet §12.13. CryptoCell-derived TRNG with a
 * 192-bit EHR (entropy holding register) split across six 32-bit
 * EHR_DATA[0..5] registers and a single VALID bit at TRNG_VALID[0]
 * that flips when all six are filled.
 *
 * Driver model:
 *   - One static 6-word cache. read_u32 returns the next cached word.
 *   - When the cache drains we re-arm: enable random source, wait
 *     for VALID, drain all six EHR words into the cache, disable.
 *   - The hardware drives the cache rather than the other way
 *     around: callers never deal with EHR ordering, valid bits, or
 *     timing.
 *
 * Why drain all six EHR words each cycle rather than reading one
 * and disabling: the EHR is filled atomically — once VALID asserts,
 * all six registers hold fresh bits. Throwing them away wastes
 * entropy and forces another (slow) ROSC sampling pass.
 *
 * Failure mode: if VALID never asserts inside our spin budget we
 * report TIKU_TRNG_ERR_TIMEOUT. The cache state stays "empty" so a
 * later call retries from scratch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"
#include "tiku_rp2350_regs.h"

/*
 * Number of ROSC clocks per sample. Higher = better whitening,
 * slower. pico-sdk uses ~0x4E20 (20 000) which gives the EHR fill
 * in ~120 ms; we drop to a more responsive value because TikuOS
 * callers tend to want one word now, not 192 bits a moment from now.
 * Tunable if AUTOCORR_STATISTIC trips for downstream users.
 */
#define TRNG_SAMPLE_COUNT       0x0064U   /* 100 ROSC cycles/sample */

/*
 * Maximum spin cycles waiting for VALID after enabling the random
 * source. ~10 ms of headroom at 150 MHz — well over the worst-case
 * EHR fill at SAMPLE_COUNT=100.
 */
#define TRNG_FILL_SPIN_LIMIT    1500000UL

#define TRNG_CACHE_WORDS        6U  /* size of EHR_DATA[0..5] */

static uint32_t trng_cache[TRNG_CACHE_WORDS];
static uint8_t  trng_cache_used = TRNG_CACHE_WORDS; /* "empty" sentinel */
static uint8_t  trng_initialised;

static int
trng_refill(void)
{
    unsigned long spin;
    unsigned int  i;

    /* Stop the source so writes to CONFIG / SAMPLE_CNT1 take effect. */
    _RP2350_REG(RP2350_TRNG_RND_SOURCE_ENABLE) = 0U;

    /* Clear pending interrupt status bits — datasheet says writing
     * the same bit pattern to ICR clears the corresponding ISR. We
     * clear everything we know about (EHR_VALID + a few error
     * bits); leftover bits in higher positions are reserved. */
    _RP2350_REG(RP2350_TRNG_TRNG_ICR) = 0x3FU;

    /* Fastest ROSC chain (selector 0). Raise if statistical tests
     * downstream report bias. */
    _RP2350_REG(RP2350_TRNG_CONFIG)   = 0U;
    _RP2350_REG(RP2350_TRNG_SAMPLE_CNT1) = TRNG_SAMPLE_COUNT;

    /* Arm. */
    _RP2350_REG(RP2350_TRNG_RND_SOURCE_ENABLE) = 1U;

    /* Spin on EHR_VALID. */
    for (spin = 0; spin < TRNG_FILL_SPIN_LIMIT; ++spin) {
        if (_RP2350_REG(RP2350_TRNG_VALID) & RP2350_TRNG_VALID_EHR_BIT) {
            break;
        }
    }
    if (spin >= TRNG_FILL_SPIN_LIMIT) {
        _RP2350_REG(RP2350_TRNG_RND_SOURCE_ENABLE) = 0U;
        return TIKU_TRNG_ERR_TIMEOUT;
    }

    /* Drain all six EHR words. Reading them clears VALID. */
    trng_cache[0] = _RP2350_REG(RP2350_TRNG_EHR_DATA0);
    trng_cache[1] = _RP2350_REG(RP2350_TRNG_EHR_DATA1);
    trng_cache[2] = _RP2350_REG(RP2350_TRNG_EHR_DATA2);
    trng_cache[3] = _RP2350_REG(RP2350_TRNG_EHR_DATA3);
    trng_cache[4] = _RP2350_REG(RP2350_TRNG_EHR_DATA4);
    trng_cache[5] = _RP2350_REG(RP2350_TRNG_EHR_DATA5);

    /* Idle the source — entropy preserved in the cache; we'll
     * re-enable next refill. */
    _RP2350_REG(RP2350_TRNG_RND_SOURCE_ENABLE) = 0U;

    /* Defence in depth: if some pathological state delivered an
     * all-zero or all-ones 192-bit fill, treat as failure and let
     * the caller retry. Real fills almost never look like this. */
    {
        uint32_t and_all = 0xFFFFFFFFU;
        uint32_t or_all  = 0U;
        for (i = 0; i < TRNG_CACHE_WORDS; ++i) {
            and_all &= trng_cache[i];
            or_all  |= trng_cache[i];
        }
        if (or_all == 0U || and_all == 0xFFFFFFFFU) {
            return TIKU_TRNG_ERR_NOT_READY;
        }
    }

    trng_cache_used = 0;
    return TIKU_TRNG_OK;
}

void
tiku_trng_arch_init(void)
{
    if (trng_initialised) {
        return;
    }

    rp2350_unreset(RP2350_RESETS_TRNG);

    /* Mark the cache empty so the first read does a hardware refill.
     * We do not preemptively refill here: the boot-time entropy
     * budget is tight and a refill takes ~milliseconds. The first
     * caller pays the cost, not boot. */
    trng_cache_used   = TRNG_CACHE_WORDS;
    trng_initialised  = 1;
}

int
tiku_trng_arch_read_u32(uint32_t *out)
{
    int rc;

    if (out == 0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_initialised) {
        tiku_trng_arch_init();
    }

    if (trng_cache_used >= TRNG_CACHE_WORDS) {
        rc = trng_refill();
        if (rc != TIKU_TRNG_OK) {
            return rc;
        }
    }
    *out = trng_cache[trng_cache_used++];
    return TIKU_TRNG_OK;
}

int
tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    size_t   i = 0;
    uint32_t word = 0;
    int      rc;
    uint8_t  word_used = 4; /* 4 bytes pending in `word`; 4 = empty */

    if (buf == 0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_initialised) {
        tiku_trng_arch_init();
    }

    while (i < len) {
        if (word_used >= 4U) {
            rc = tiku_trng_arch_read_u32(&word);
            if (rc != TIKU_TRNG_OK) {
                return rc;
            }
            word_used = 0;
        }
        buf[i++] = (uint8_t)(word & 0xFFU);
        word   >>= 8;
        word_used++;
    }
    return TIKU_TRNG_OK;
}
