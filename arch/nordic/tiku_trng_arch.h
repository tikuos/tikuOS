/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - nRF54L true random number generator (CRACEN RNG).
 *
 * Reads block polling the CRACEN entropy FIFO; a hardware stall returns
 * TIKU_TRNG_ERR_TIMEOUT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_TRNG_ARCH_H_
#define TIKU_NORDIC_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/* Return codes for the TRNG driver. */
#define TIKU_TRNG_OK             0  /**< success                           */
#define TIKU_TRNG_ERR_INVALID   -1  /**< NULL pointer                      */
#define TIKU_TRNG_ERR_TIMEOUT   -2  /**< the RNG FIFO did not deliver      */
#define TIKU_TRNG_ERR_NOT_READY -3  /**< not returned by this port         */

/**
 * @brief Mark the driver initialised; the CRACEN RNG module is enabled only
 *        for the duration of each read.
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
 * @brief Fill a byte buffer with @p len hardware random bytes.
 *
 * @param buf  Destination buffer. Must not be NULL.
 * @param len  Number of bytes requested.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if @p buf is
 *         NULL, or TIKU_TRNG_ERR_TIMEOUT if the RNG stalled (@p buf may
 *         then be partly written).
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_NORDIC_TRNG_ARCH_H_ */
