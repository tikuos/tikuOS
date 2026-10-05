/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - ESP32-C61 random words from the LP RNG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_TRNG_ARCH_H_
#define TIKU_ESP32C61_TRNG_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_TRNG_OK             0  /**< Random data delivered */
#define TIKU_TRNG_ERR_INVALID   -1  /**< NULL output pointer */
#define TIKU_TRNG_ERR_TIMEOUT   -2  /**< Not returned on this port */
#define TIKU_TRNG_ERR_NOT_READY -3  /**< No fresh samples in the poll bound */

/** @brief Start the noise sampling that feeds the generator. */
void tiku_trng_arch_init(void);

/**
 * @brief Read one word once fresh samples have been mixed in.
 *
 * @return TIKU_TRNG_OK, TIKU_TRNG_ERR_INVALID for a NULL @p out, or
 *         TIKU_TRNG_ERR_NOT_READY when no fresh samples arrive
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/** @brief Fill @p buf with random bytes. @return TIKU_TRNG_OK or an error */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

#endif /* TIKU_ESP32C61_TRNG_ARCH_H_ */
