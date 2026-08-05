/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xflash_arch.h - EK-RA8P1 Octo-SPI NOR (MX25LW51245G, 64 MB).
 *
 * Phase 1: controller and pins up, device identified over plain 1-1-1 SPI,
 * which is the mode the part powers up in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_XFLASH_ARCH_H_
#define TIKU_RA8P1_XFLASH_ARCH_H_

#include <stdint.h>

#define TIKU_RA8P1_XFLASH_OK          0
#define TIKU_RA8P1_XFLASH_ERR_TIMEOUT -1  /**< transaction never completed  */
#define TIKU_RA8P1_XFLASH_ERR_ID      -2  /**< no Macronix device answered  */

/** @brief Mapped window and capacity of the board's part. */
#define TIKU_RA8P1_XFLASH_ADDR   0x70000000UL
#define TIKU_RA8P1_XFLASH_BYTES  (64UL * 1024UL * 1024UL)

/** @brief Bring up the OSPI1 controller and its pins.  Idempotent. */
void tiku_ra8p1_xflash_init(void);

/**
 * @brief Read the JEDEC ID over 1-1-1 SPI.
 *
 * The identifying transaction, and deliberately the first one: it proves
 * pins, clock and controller together, and its answer is self-checking
 * because only one manufacturer byte is correct.
 *
 * @param out  Receives 3 bytes: manufacturer, memory type, density
 * @return TIKU_RA8P1_XFLASH_OK, or a negative error code
 */
int tiku_ra8p1_xflash_read_id(uint8_t out[3]);

#endif /* TIKU_RA8P1_XFLASH_ARCH_H_ */
