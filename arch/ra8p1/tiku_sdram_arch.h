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

/** @brief Result codes; negative values are failures. */
#define TIKU_RA8P1_SDRAM_OK          0
#define TIKU_RA8P1_SDRAM_ERR_INIT   -1   /**< a sequencer timed out       */
#define TIKU_RA8P1_SDRAM_ERR_CLOCK  -2   /**< BCLK out of range, SDCLK off */

/** @brief Base and size of the mapped window. */
#define TIKU_RA8P1_SDRAM_ADDR   0x68000000UL
#define TIKU_RA8P1_SDRAM_BYTES  (64UL * 1024UL * 1024UL)

/**
 * @brief Configure the pins, controller and part; leaves the window usable.
 *
 * Idempotent.  Runs the datasheet's power-up: 200 us of running clock, then
 * precharge-all and eight auto-refreshes from the controller's sequencer,
 * then the mode register.  Timings are set for the BCLK in force at the call.
 *
 * @return TIKU_RA8P1_SDRAM_OK, or a negative error code
 */
int tiku_ra8p1_sdram_init(void);

/** @brief Non-zero once tiku_ra8p1_sdram_init() has succeeded. */
int tiku_ra8p1_sdram_ready(void);

/**
 * @brief Set the refresh interval for the BCLK in force now.
 *
 * Called after a change of clock rung.  Does nothing before
 * tiku_ra8p1_sdram_init() has succeeded.  The access timings stay as init
 * set them.
 */
void tiku_ra8p1_sdram_retune(void);

/**
 * @brief Bring the array up and hand it to the tier allocator.
 *
 * Attaches the window as TIKU_MEM_PSRAM, the tier for a large external
 * volatile memory that exists once its controller is up.
 *
 * @return TIKU_RA8P1_SDRAM_OK, an init error, or TIKU_RA8P1_SDRAM_ERR_INIT
 *         when the tier refuses the window, as it does a second attach
 */
int tiku_ra8p1_sdram_attach(void);

/**
 * @brief Time sequential, strided and memcpy legs and print each by name.
 *
 * Refused while the PSRAM tier holds any allocation.
 *
 * @note Overwrites the first 1 MB of the window, which is also the start of
 *       the USB staging disk.
 */
void tiku_ra8p1_sdram_bench_run(void);

#endif /* TIKU_RA8P1_SDRAM_ARCH_H_ */
