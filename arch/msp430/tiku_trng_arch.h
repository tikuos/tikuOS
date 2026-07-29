/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - MSP430 software entropy source (no hardware TRNG).
 *
 * Synthesises a TRNG from two on-die noise sources so the same blocking-read API
 * as the hardware-TRNG ports works here.  A stuck source returns ERR_TIMEOUT and
 * TLS fails closed.  Collection is slow, so seed a DRBG once rather than looping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MSP430_TRNG_ARCH_H_
#define TIKU_MSP430_TRNG_ARCH_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Return codes for the TRNG driver (shared with the HW-TRNG ports).
 *
 * TIKU_TRNG_OK            — success.
 * TIKU_TRNG_ERR_INVALID   — NULL pointer or zero-length buffer.
 * TIKU_TRNG_ERR_TIMEOUT   — a health test failed (dead timer / stuck
 *                           jitter source); the output buffer is left
 *                           unmodified.
 * TIKU_TRNG_ERR_NOT_READY — tiku_trng_arch_init() was not called (unused
 *                           here: reads auto-init).
 */
#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/**
 * @brief One-time init: configure the ADC for the thermal-noise source.
 *        Idempotent; auto-called on the first read.
 */
void tiku_trng_arch_init(void);

/**
 * @brief Fill @p buf with @p len cryptographically-conditioned random
 *        bytes.  Fails closed (returns TIKU_TRNG_ERR_TIMEOUT, buffer
 *        partially written) if a health test trips.
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

/**
 * @brief Blocking read of one 32-bit random word.
 */
int tiku_trng_arch_read_u32(uint32_t *out);

#endif /* TIKU_MSP430_TRNG_ARCH_H_ */
