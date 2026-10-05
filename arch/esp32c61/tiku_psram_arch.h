/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.h - ESP32-C61 in-package PSRAM: the quad device behind
 * MSPI chip select 1, mapped through the cache beside the flash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_PSRAM_ARCH_H_
#define TIKU_ESP32C61_PSRAM_ARCH_H_

#include <stdint.h>

/** Where the PSRAM appears: the MMU window past the flash's 8 MB. */
#define TIKU_ESP32C61_PSRAM_BASE    0x42800000UL

/** BASIC native modules run from the PSRAM's first 32 KB; with the loader in
 *  the build, the tier is handed only what lies past it. */
#define TIKU_ESP32C61_MODULE_WINDOW       TIKU_ESP32C61_PSRAM_BASE
#define TIKU_ESP32C61_MODULE_WINDOW_BYTES 0x8000UL

typedef enum {
    TIKU_ESP32C61_PSRAM_OK      =  0,
    TIKU_ESP32C61_PSRAM_ABSENT  = -1,   /**< no answer on chip select 1 */
    TIKU_ESP32C61_PSRAM_BAD_ID  = -2,   /**< answered, but not a known part */
    TIKU_ESP32C61_PSRAM_MAP     = -3,   /**< the MMU refused the pages */
    TIKU_ESP32C61_PSRAM_VERIFY  = -4,   /**< mapped, but a pattern did not hold */
} tiku_esp32c61_psram_err_t;

/**
 * @brief Bring the device up in QPI mode and map it at the PSRAM base.
 *
 * Idempotent.  The SPI1 state the flash routines rely on is restored before
 * returning, whatever the outcome.
 */
tiku_esp32c61_psram_err_t tiku_esp32c61_psram_init(void);

/** @brief Init, then hand the mapped bytes to the PSRAM tier, past any
 *         module window and buffers placed there; idempotent. */
tiku_esp32c61_psram_err_t tiku_esp32c61_psram_attach(void);

/** @brief At boot, in a build with buffers in PSRAM (psram_data.ld): bring
 *         it up and zero them, or halt saying why. */
void tiku_esp32c61_psram_data_boot(void);

/** @brief The device's 24-bit ID (MFID, KGD, density), 0 before init. */
uint32_t tiku_esp32c61_psram_id(void);

/** @brief Mapped bytes, 0 until init succeeds. */
uint32_t tiku_esp32c61_psram_size(void);

#endif /* TIKU_ESP32C61_PSRAM_ARCH_H_ */
