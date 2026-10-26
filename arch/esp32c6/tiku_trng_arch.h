/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - C6 RNG with the internal SAR entropy source.
 *
 * A read is at most 256 bytes and runs with interrupts masked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C6_TRNG_ARCH_H_
#define TIKU_ESP32C6_TRNG_ARCH_H_
#include <stddef.h>
#include <stdint.h>
#define TIKU_TRNG_OK            0
#define TIKU_TRNG_ERR_INVALID   -1
#define TIKU_TRNG_ERR_TIMEOUT   -2
#define TIKU_TRNG_ERR_NOT_READY -3
/** @brief Initialize software state; hardware is acquired per read. */
void tiku_trng_arch_init(void);
/** @brief Read a word with SAR entropy; return a negative error on failure. */
int tiku_trng_arch_read_u32(uint32_t *out);
/**
 * @brief Fill up to 256 bytes of entropy, then restore hardware.
 *
 * The internal SAR channel supplies noise. Active conversions or temperature
 * sensing refuse the read. Hardware state is restored; errors clear output.
 * @note Requires foreground context.
 */
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length);
#endif
