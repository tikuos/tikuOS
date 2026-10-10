/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trng_arch.h - C5 RNG with the internal SAR entropy source.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_TRNG_ARCH_H_
#define TIKU_ESP32C5_TRNG_ARCH_H_
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
 * @brief Fill up to 256 bytes using internal SAR entropy, then restore
 * hardware. Requires foreground context; active ADC or PHY ownership refuses
 * the read. Errors clear output. The function does not use radio-supplied
 * entropy.
 */
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length);
#endif
