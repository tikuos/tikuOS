/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - RP2350 TRNG driver.
 *
 * Each refill reads all six entropy holding register (EHR) words into a static
 * cache, and reads take words from the cache until it is empty.  A refill
 * whose VALID flag never rises returns TIKU_TRNG_ERR_TIMEOUT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trng_arch.h"
#include "tiku_rp2350_regs.h"

/*
 * TRNG_SAMPLE_COUNT is written to SAMPLE_CNT1: the rng_clk cycles between two
 * ring-oscillator samples.  A larger count decorrelates the samples further
 * and makes each refill slower.
 *
 * TRNG_FILL_SPIN_LIMIT is how many times a refill polls VALID before it
 * returns TIKU_TRNG_ERR_TIMEOUT.
 */
#define TRNG_SAMPLE_COUNT       0x0064U   /**< rng_clk cycles per sample */
#define TRNG_FILL_SPIN_LIMIT    1500000UL /**< VALID polls per refill */
#define TRNG_CACHE_WORDS        6U        /**< EHR_DATA[0..5] word count */

/** @brief Cached EHR words, drained from hardware on each refill. */
static uint32_t trng_cache[TRNG_CACHE_WORDS];
/** @brief Next unread index into trng_cache; TRNG_CACHE_WORDS = empty. */
static uint8_t  trng_cache_used = TRNG_CACHE_WORDS;
/** @brief Non-zero once tiku_trng_arch_init() has succeeded. */
static uint8_t  trng_initialised;

/**
 * @brief Refill the EHR cache from the TRNG hardware.
 *
 * Stops the random source, clears pending IRQ status, programs TRNG_CONFIG and
 * SAMPLE_CNT1 and re-arms.  Spins on EHR_VALID up to TRNG_FILL_SPIN_LIMIT
 * times, then reads all six EHR_DATA registers into trng_cache and stops.
 *
 * @note An all-zero or all-ones 192-bit fill is rejected and the cache stays
 *       empty, so the next read refills again.
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

    /* Writing a 1 to an ICR bit clears that bit of RNG_ISR: EHR_VALID,
     * CRNGT_ERR and VN_ERR.  AUTOCORR_ERR clears only on a TRNG reset. */
    _RP2350_REG(RP2350_TRNG_TRNG_ICR) = 0x3FU;

    /* Ring-oscillator selector 0, the fastest chain. */
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

    /* Stop the source until the next refill. */
    _RP2350_REG(RP2350_TRNG_RND_SOURCE_ENABLE) = 0U;

    /* Reject an all-zero or all-ones fill; the cache stays empty. */
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
 * Brings the TRNG out of reset and marks the cache empty; the first read
 * fills it.  A second call returns at once.
 */
void
tiku_trng_arch_init(void)
{
    if (trng_initialised) {
        return;
    }

    rp2350_unreset(RP2350_RESETS_TRNG);

    /* Mark the cache empty so the first read does a hardware refill. */
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
 * Consumes the EHR cache word by word, extracting bytes little-endian; a
 * partial final word is used up to the requested length and then discarded.
 * Calls tiku_trng_arch_init() lazily if needed.
 *
 * @param buf  Destination buffer (must be non-NULL).
 * @param len  Number of random bytes to produce.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if buf is NULL or
 *         len is 0, or a tiku_trng_arch_read_u32() error code on failure.
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
