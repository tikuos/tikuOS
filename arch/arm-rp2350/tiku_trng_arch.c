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

/**
 * @defgroup trng_config TRNG private configuration
 * @brief Tuning constants for the RP2350 TRNG driver.
 *
 * TRNG_SAMPLE_COUNT controls ROSC clocks per sample: higher values
 * improve whitening at the cost of latency.  pico-sdk uses ~0x4E20
 * (120 ms EHR fill); TikuOS uses 100 for responsiveness.  Raise if
 * AUTOCORR_STATISTIC trips downstream.
 *
 * TRNG_FILL_SPIN_LIMIT is the maximum spin cycles waiting for VALID
 * after arming the random source.  ~10 ms headroom at 150 MHz is well
 * above the worst-case EHR fill at SAMPLE_COUNT=100.
 *
 * TRNG_CACHE_WORDS is the number of 32-bit EHR data registers (0..5).
 * @{
 */
#define TRNG_SAMPLE_COUNT       0x0064U   /**< ROSC cycles per sample */
#define TRNG_FILL_SPIN_LIMIT    1500000UL /**< Spin budget for EHR fill */
#define TRNG_CACHE_WORDS        6U        /**< EHR_DATA[0..5] word count */
/** @} */

/** @brief Cached EHR words, drained from hardware on each refill. */
static uint32_t trng_cache[TRNG_CACHE_WORDS];
/** @brief Next unread index into trng_cache; TRNG_CACHE_WORDS = empty. */
static uint8_t  trng_cache_used = TRNG_CACHE_WORDS;
/** @brief Non-zero once tiku_trng_arch_init() has succeeded. */
static uint8_t  trng_initialised;

/**
 * @brief Refill the EHR cache from the TRNG hardware.
 *
 * Stops the random source, clears pending IRQ status, programs
 * SAMPLE_CNT1, and re-arms the source.  Spins on EHR_VALID up to
 * TRNG_FILL_SPIN_LIMIT cycles, then drains all six EHR_DATA registers
 * into trng_cache and disables the source.
 *
 * As a defence-in-depth sanity check the function rejects an all-zero
 * or all-ones 192-bit fill and returns TIKU_TRNG_ERR_NOT_READY, leaving
 * trng_cache_used at TRNG_CACHE_WORDS so the next call retries from
 * scratch.
 *
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_TIMEOUT if EHR_VALID
 *         never asserted within the spin budget, TIKU_TRNG_ERR_NOT_READY
 *         if the fill was all-zero or all-ones.
 */
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

/**
 * @brief Initialize the RP2350 TRNG peripheral.
 *
 * Brings the TRNG out of reset and marks the cache empty.  The EHR
 * is not prefilled here: boot-time entropy budget is tight and a
 * refill takes milliseconds.  The first tiku_trng_arch_read_u32()
 * caller pays the cost.  Idempotent: subsequent calls return early.
 */
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

/**
 * @brief Read one 32-bit random word from the TRNG.
 *
 * Returns the next word from the EHR cache, triggering a hardware
 * refill (trng_refill()) when the cache is exhausted.  Calls
 * tiku_trng_arch_init() lazily if not already initialized.
 *
 * @param out  Destination for the random word (must be non-NULL).
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if out is
 *         NULL, or a trng_refill() error code on hardware failure.
 */
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

/**
 * @brief Fill a byte buffer with random data from the TRNG.
 *
 * Consumes the EHR cache word-by-word, extracting bytes in
 * little-endian order.  A partial final word is used up to the
 * requested length and then discarded.  Calls tiku_trng_arch_init()
 * lazily if not already initialized.
 *
 * @param buf  Destination buffer (must be non-NULL).
 * @param len  Number of random bytes to produce.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if buf is
 *         NULL, or a tiku_trng_arch_read_u32() error code on failure.
 */
int
tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    size_t   i = 0;
    uint32_t word = 0;
    int      rc;
    uint8_t  word_used = 4; /* 4 bytes pending in `word`; 4 = empty */

    if (buf == 0 || len == 0U) {
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
