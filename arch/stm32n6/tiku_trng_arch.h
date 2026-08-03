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

#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/** @brief Clock and start the generator; safe to call more than once. */
void tiku_trng_arch_init(void);

/**
 * @brief Read one random word.
 *
 * @param out  Receives the word; must not be NULL
 * @return TIKU_TRNG_OK, or a negative error
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a buffer with random bytes.
 *
 * @param buf  Destination; must not be NULL
 * @param len  Byte count
 * @return TIKU_TRNG_OK, or a negative error
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_STM32N6_TRNG_ARCH_H_ */
