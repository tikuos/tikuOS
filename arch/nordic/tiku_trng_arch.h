/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - nRF54L True Random Number Generator (stub — not yet wired)
 *
 * Mirrors the RP2350 TRNG arch API (return codes plus init /
 * read_u32 / read_bytes) so shared callers link unchanged. The nRF54L
 * has a hardware entropy source (CRACEN RNG), but no backend is wired
 * yet; the current implementation returns TIKU_TRNG_ERR_NOT_READY and
 * never fabricates random bytes. A real backend is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_TRNG_ARCH_H_
#define TIKU_NORDIC_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Return codes for the TRNG driver (identical to the other ports).
 *
 * TIKU_TRNG_OK            — success.
 * TIKU_TRNG_ERR_INVALID   — NULL pointer.
 * TIKU_TRNG_ERR_TIMEOUT   — hardware did not assert VALID in time.
 * TIKU_TRNG_ERR_NOT_READY — no entropy backend available (stub state).
 */
#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init (stub — no-op). Idempotent.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Fetch a 32-bit random word.
 *
 * @param out  Where to store the word. Must not be NULL.
 * @return TIKU_TRNG_ERR_INVALID if @p out is NULL, otherwise
 *         TIKU_TRNG_ERR_NOT_READY (no entropy source wired yet);
 *         *out is left untouched.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` random bytes.
 *
 * @param buf  Destination buffer. Must not be NULL.
 * @param len  Number of bytes requested.
 * @return TIKU_TRNG_ERR_INVALID if @p buf is NULL, otherwise
 *         TIKU_TRNG_ERR_NOT_READY (no entropy source wired yet);
 *         @p buf is left untouched.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_NORDIC_TRNG_ARCH_H_ */
