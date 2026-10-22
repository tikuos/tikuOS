/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.h - Boot-only P4 AP x16 PSRAM initialization.
 *
 * The fitted 32 MiB PSRAM maps at TIKU_P4_PSRAM_BASE once
 * tiku_p4_psram_init() succeeds; after a failed attempt it refuses until
 * reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_PSRAM_ARCH_H_
#define TIKU_P4_PSRAM_ARCH_H_
#include <stdint.h>
#define TIKU_P4_PSRAM_BASE 0x48000000u
#define TIKU_P4_PSRAM_BYTES 0x02000000u
/** @brief Initialize the fitted AP 32 MiB PSRAM at 80 MHz; zero means success.
 * @note CPU0 SRAM execution, parked other cores, no mapped flash/PSRAM users
 * or DMA. Failure requires reset before retry. Initialization writes 64 low-lane
 * bytes in the first 128 bytes; the caller must reserve that range until return.
 */
int tiku_p4_psram_init(void);
/** @brief Return capacity after identity, PIO and mapped checks, or zero. */
uint32_t tiku_p4_psram_size(void);
/** @brief Set the bus clock for a CPU at @p cpu_mhz: 200 MHz under 360,
 *         then 100, 66 and 40 MHz, never above the CPU clock. */
void tiku_p4_psram_follow_cpu(unsigned cpu_mhz);
/** @brief The bus clock in Hz, 0 without PSRAM. */
uint32_t tiku_p4_psram_hz(void);
/** @brief The calibration: the strobe phases (4 bits) and delay entries
 *         (31 bits) that read the pattern back, and the two chosen. */
void tiku_p4_psram_calibration(uint32_t *phases, uint32_t *delays,
                               unsigned *phase, unsigned *delay);
/** @brief Return the last completed initialization stage for diagnostics. */
unsigned tiku_p4_psram_stage(void);
#endif
