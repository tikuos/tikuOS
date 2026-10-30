/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.h - C6 RNG with SAR or calibrated RF entropy.
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
typedef enum {
    TIKU_C6_TRNG_RADIO_OFF,
    TIKU_C6_TRNG_RADIO_RF,
    TIKU_C6_TRNG_RADIO_BUSY
} tiku_c6_trng_radio_t;
/** @brief Select SAR availability, calibrated RF entropy, or a PHY transition
 * that refuses entropy reads. Called by the radio owner around PHY changes. */
void tiku_c6_trng_radio_set(tiku_c6_trng_radio_t source);
/** @brief Initialize software state; hardware is acquired per read. */
void tiku_trng_arch_init(void);
/** @brief Read a word from the current entropy source; return a negative error on failure. */
int tiku_trng_arch_read_u32(uint32_t *out);
/**
 * @brief Fill up to 256 bytes of entropy, then restore hardware.
 *
 * Calibrated RF supplies noise while selected; otherwise the internal SAR
 * channel is acquired. PHY transitions and occupied SAR refuse reads.
 * Hardware state is restored; source failures clear valid-sized output.
 * @note Requires foreground context.
 */
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length);
#endif
