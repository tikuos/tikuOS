/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - RA8P1 entropy source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_TRNG_ARCH_H_
#define TIKU_RA8P1_TRNG_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_TRNG_OK             0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3

/** @brief Prepare the entropy source; safe to call more than once. */
void tiku_trng_arch_init(void);

/**
 * @brief Read one conditioned random word.
 *
 * @param out  Receives the word
 * @return TIKU_TRNG_OK, or a negative error
 */
int tiku_trng_arch_read_u32(uint32_t *out);

/**
 * @brief Fill a buffer with conditioned random bytes.
 *
 * @param buf  Destination
 * @param len  Bytes wanted
 * @return TIKU_TRNG_OK, or a negative error
 */
int tiku_trng_arch_read_bytes(uint8_t *buf, size_t len);

/**
 * @brief Raw ratio counts, for judging the source rather than using it.
 *
 * Conditioning hides a dead source: SHA-256 of a constant is a constant that
 * looks random, so the unconditioned counts are the only view that shows
 * whether the oscillators disagree at all.
 *
 * @param out    Destination counts
 * @param n      How many to take
 * @return TIKU_TRNG_OK, or a negative error
 */
int tiku_trng_arch_raw_counts(uint16_t *out, size_t n);

#endif /* TIKU_RA8P1_TRNG_ARCH_H_ */
