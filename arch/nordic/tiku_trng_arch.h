/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - nRF54L15 True Random Number Generator (CRACEN RNG)
 *
 * Mirrors the RP2350 TRNG arch API (return codes plus init /
 * read_u32 / read_bytes) so shared callers link unchanged. The backend
 * drives the ring-oscillator TRNG inside CRACEN with AES conditioning
 * (see tiku_trng_arch.c); it blocks-polls the entropy FIFO and never
 * fabricates random bytes -- on a hardware stall it returns
 * TIKU_TRNG_ERR_TIMEOUT rather than substituting pseudo-random data.
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
 * TIKU_TRNG_ERR_TIMEOUT   — the RNG FIFO did not deliver in time.
 * TIKU_TRNG_ERR_NOT_READY — reserved (kept for cross-port API parity).
 */
#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init. Idempotent; the RNG is powered per request.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Fetch a 32-bit random word (little-endian byte packing).
 *
 * @param out  Where to store the word. Must not be NULL.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if @p out is
 *         NULL, or TIKU_TRNG_ERR_TIMEOUT if the RNG did not deliver;
 *         *out is left untouched on error.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` hardware random bytes.
 *
 * @param buf  Destination buffer. Must not be NULL.
 * @param len  Number of bytes requested.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if @p buf is
 *         NULL, or TIKU_TRNG_ERR_TIMEOUT if the RNG stalled (@p buf may
 *         be partially written; no pseudo-random data is substituted).
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_NORDIC_TRNG_ARCH_H_ */
