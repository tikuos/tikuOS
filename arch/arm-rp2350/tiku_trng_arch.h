/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - RP2350 true random number generator HAL.
 *
 * Blocking reads over the TRNG block.  A 192-bit cache holds one hardware
 * fill, and a read starts a new fill only when the cache is empty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_TRNG_ARCH_H_
#define TIKU_RP2350_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_TRNG_OK            0  /**< Success */
#define TIKU_TRNG_ERR_INVALID  -1  /**< NULL pointer or zero-length buffer */
#define TIKU_TRNG_ERR_TIMEOUT  -2  /**< VALID never rose during a refill */
#define TIKU_TRNG_ERR_NOT_READY -3 /**< A refill read all-zero or all-ones
                                        words */

/**
 * @brief Bring the TRNG block out of reset and mark the cache empty.
 *
 * A second call returns at once; the read functions call it when needed.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Block until a 32-bit random word is available; return it.
 *
 * Takes the next word from the six-word cache.  An empty cache is refilled
 * first: the driver arms the hardware and polls VALID up to 1,500,000 times.
 *
 * @param out  Where to store the random word. Must not be NULL.
 * @return TIKU_TRNG_OK, TIKU_TRNG_ERR_INVALID, TIKU_TRNG_ERR_TIMEOUT or
 *         TIKU_TRNG_ERR_NOT_READY; *out is written only on TIKU_TRNG_OK.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a byte buffer with `len` random bytes.
 *
 * Takes words from tiku_trng_arch_read_u32() and stores their bytes, low
 * byte first.  On an error it stops; the bytes already stored stay.
 *
 * @param buf  Destination buffer.
 * @param len  Number of bytes to write.
 * @return TIKU_TRNG_OK, TIKU_TRNG_ERR_INVALID for a NULL buffer or zero
 *         length, or the error from tiku_trng_arch_read_u32().
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_RP2350_TRNG_ARCH_H_ */
