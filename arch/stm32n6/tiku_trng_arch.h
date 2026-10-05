/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - STM32N6 hardware random number generator.
 *
 * Entropy comes from the RNG block's ring oscillators, so the numbers are not
 * reproducible and need no seeding.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_TRNG_ARCH_H_
#define TIKU_STM32N6_TRNG_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_TRNG_OK             0  /**< success                         */
#define TIKU_TRNG_ERR_INVALID   -1  /**< NULL destination                */
#define TIKU_TRNG_ERR_TIMEOUT   -2  /**< no word ready in time           */
#define TIKU_TRNG_ERR_NOT_READY -3  /**< not returned on this port       */

/** @brief Clock the RNG, pulse its conditioning reset and enable it. */
void tiku_trng_arch_init(void);

/**
 * @brief Read one random word, starting the generator on first use.
 *
 * A latched seed or clock error reconditions the generator before the read.
 *
 * @param out  Receives the word
 * @return TIKU_TRNG_OK, TIKU_TRNG_ERR_INVALID for NULL @p out, or
 *         TIKU_TRNG_ERR_TIMEOUT
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a buffer with random bytes.
 *
 * @param buf  Destination
 * @param len  Byte count
 * @return TIKU_TRNG_OK, TIKU_TRNG_ERR_INVALID for NULL @p buf, or the first
 *         error of tiku_trng_arch_read_u32(), with @p buf partly filled
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_STM32N6_TRNG_ARCH_H_ */
