/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - nRF54L True Random Number Generator (stub — not yet wired)
 *
 * Honest placeholder for the CRACEN RNG entropy source. read_u32 and
 * read_bytes report TIKU_TRNG_ERR_NOT_READY and never fabricate random
 * bytes (the caller's buffer is left untouched); init is a no-op. A
 * real entropy backend is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_trng_arch.h>

/**
 * @brief Initialise the TRNG block (stub — no-op).
 *
 * A real backend brings the CRACEN RNG out of reset and primes the
 * entropy path here. Idempotent.
 */
void tiku_trng_arch_init(void)
{
}

/**
 * @brief Read one 32-bit random word (stub).
 *
 * Never produces entropy: *out is left untouched so no fabricated
 * value can be mistaken for randomness.
 *
 * @param out  Destination for the random word (must be non-NULL).
 * @return TIKU_TRNG_ERR_INVALID if @p out is NULL, otherwise
 *         TIKU_TRNG_ERR_NOT_READY.
 */
int tiku_trng_arch_read_u32(uint32_t *out)
{
    if (out == (uint32_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    return TIKU_TRNG_ERR_NOT_READY;
}

/**
 * @brief Fill a byte buffer with random data (stub).
 *
 * Never produces entropy: @p buf is left untouched.
 *
 * @param buf  Destination buffer (must be non-NULL).
 * @param len  Number of random bytes requested (ignored).
 * @return TIKU_TRNG_ERR_INVALID if @p buf is NULL, otherwise
 *         TIKU_TRNG_ERR_NOT_READY.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    (void)len;
    if (buf == (uint8_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    return TIKU_TRNG_ERR_NOT_READY;
}
