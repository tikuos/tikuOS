/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - RP2350 True Random Number Generator HAL
 *
 * Wraps the dedicated TRNG block (datasheet §12.13). One blocking
 * read API; a 192-bit ring buffer in static RAM is kept full
 * automatically across calls so the typical read_u32() returns from
 * RAM and only re-arms the hardware when the cache is empty.
 *
 * Use cases:
 *   - Seed for the existing software PRNG in tikukits/crypto/
 *   - Nonce / IV for ephemeral session keys
 *   - One-time stack canary refresh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_TRNG_ARCH_H_
#define TIKU_RP2350_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_TRNG_OK            0
#define TIKU_TRNG_ERR_INVALID  -1
#define TIKU_TRNG_ERR_TIMEOUT  -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init: bring the TRNG block out of reset and
 *        configure the entropy source. Idempotent.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Block until a 32-bit random word is available; return it.
 *
 * On the fast path the word comes from the 6-word software cache
 * already filled by the hardware. On the slow path the hardware is
 * armed and we spin on the VALID flag for up to a few thousand
 * cycles before giving up — see TIKU_TRNG_ERR_TIMEOUT.
 *
 * @param out  Where to store the random word. Must not be NULL.
 * @return TIKU_TRNG_OK or a negative error code.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` random bytes.
 *
 * Internally calls read_u32() repeatedly and copies bytes (any byte
 * order — bytes are uniformly random). Stops early and returns
 * TIKU_TRNG_ERR_TIMEOUT on hardware failure.
 *
 * @param buf  Destination buffer.
 * @param len  Number of bytes to write.
 * @return TIKU_TRNG_OK or a negative error code.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_RP2350_TRNG_ARCH_H_ */
