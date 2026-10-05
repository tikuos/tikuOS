/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - Ambiq true random number generator HAL.
 *
 * Wraps the CryptoCell-312 TRNG of the Apollo4 Lite, Apollo4 Plus and
 * Apollo510: blocking reads served from a 192-bit RAM cache, the API the
 * kit-side TLS binding calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_TRNG_ARCH_H_
#define TIKU_AMBIQ_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Return codes of the TRNG driver. */
#define TIKU_TRNG_OK            0
#define TIKU_TRNG_ERR_INVALID  -1   /**< NULL output pointer                */
#define TIKU_TRNG_ERR_TIMEOUT  -2   /**< EHR never valid in the retry budget */
#define TIKU_TRNG_ERR_NOT_READY -3  /**< not returned: reads call init      */

/**
 * @brief One-time init: power up the CRYPTO (CryptoCell-312) domain.
 *        Idempotent; auto-called on the first read.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Block until a 32-bit random word is available; store it in @p out.
 *
 * The fast path returns the next word from the 6-word software cache the
 * hardware already filled; the slow path re-arms the ring-oscillator source and
 * spins on EHR_VALID for a bounded budget, re-arming on a health-test failure.
 *
 * @param out  Where to store the random word. Must not be NULL.
 * @return TIKU_TRNG_OK; TIKU_TRNG_ERR_INVALID for a NULL @p out;
 *         TIKU_TRNG_ERR_TIMEOUT with *out unchanged.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` cryptographically random bytes.
 *
 * Takes one tiku_trng_arch_read_u32() word per 4 bytes, low byte first.  On
 * TIKU_TRNG_ERR_TIMEOUT the buffer may be partly written.
 *
 * @param buf  Destination buffer.
 * @param len  Number of bytes to write.
 * @return TIKU_TRNG_OK (also for @p len 0); TIKU_TRNG_ERR_INVALID for a NULL
 *         @p buf with a non-zero @p len; TIKU_TRNG_ERR_TIMEOUT.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_AMBIQ_TRNG_ARCH_H_ */
