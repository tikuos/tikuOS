/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - MSP430 software entropy source (no hardware TRNG).
 *
 * A software TRNG from two on-die noise sources, behind the blocking-read API
 * of the hardware-TRNG ports.  A stuck source fails the read.  Every 32-byte
 * block costs TRNG_POOL_ROUNDS harvest rounds: seed a DRBG from it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MSP430_TRNG_ARCH_H_
#define TIKU_MSP430_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @name TRNG return codes, shared with the hardware-TRNG ports
 * INVALID: NULL or zero-length buffer.  TIMEOUT: a health test failed and
 * the buffer was zeroed.  NOT_READY: not returned here, since a read
 * initialises the source itself.
 * @{
 */
#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3
/** @} */

/**
 * @brief One-time init: configure the ADC for the thermal-noise source.
 *        Idempotent; auto-called on the first read.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Fill @p buf with @p len cryptographically-conditioned random
 *        bytes.
 * @return TIKU_TRNG_OK; TIKU_TRNG_ERR_INVALID for a NULL or empty buffer;
 *         TIKU_TRNG_ERR_TIMEOUT when a health test trips, with all of
 *         @p buf zeroed
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

/**
 * @brief Blocking read of one 32-bit random word.
 * @return As tiku_trng_arch_read_bytes(); @p out is written only on
 *         TIKU_TRNG_OK
 */
int tiku_trng_arch_read_u32(uint32_t *out);

#endif /* TIKU_MSP430_TRNG_ARCH_H_ */
