/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdram_arch.h - EK-RA8P1 external SDRAM (IS42S32160F, 64 MB).
 *
 * Brings up the bus controller and the part, then leaves 64 MB mapped at
 * 0x6800_0000 for a tier or a bench to use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_SDRAM_ARCH_H_
#define TIKU_RA8P1_SDRAM_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_RA8P1_SDRAM_OK          0
#define TIKU_RA8P1_SDRAM_ERR_INIT   -1   /**< sequencer never completed  */
#define TIKU_RA8P1_SDRAM_ERR_CLOCK  -2   /**< bus clock outside the part */

/** @brief Base and size of the mapped window. */
#define TIKU_RA8P1_SDRAM_ADDR   0x68000000UL
#define TIKU_RA8P1_SDRAM_BYTES  (64UL * 1024UL * 1024UL)

/**
 * @brief Configure the pins, controller and part; leaves the window usable.
 *
 * Idempotent.  Runs the JEDEC power-up the datasheet requires -- 100 us of
 * stable clock, precharge-all, auto-refresh, mode register -- via the
 * controller's own initialisation sequencer.
 *
 * @return TIKU_RA8P1_SDRAM_OK, or a negative error code
 */
int tiku_ra8p1_sdram_init(void);

/** @brief Non-zero once init() has completed successfully. */
int tiku_ra8p1_sdram_ready(void);

/**
 * @brief Bring the array up and hand it to the tier allocator.
 *
 * Attaches as TIKU_MEM_PSRAM, the tier's name for a large external volatile
 * aperture that exists only once its controller is up -- the same role Ambiq's
 * PSRAM fills, so a model placed there is board-agnostic.
 *
 * @return TIKU_RA8P1_SDRAM_OK, or a negative error code
 */
int tiku_ra8p1_sdram_attach(void);

/** @brief Timed read/write/copy legs over the array, each named. */
void tiku_ra8p1_sdram_bench_run(void);

#endif /* TIKU_RA8P1_SDRAM_ARCH_H_ */
