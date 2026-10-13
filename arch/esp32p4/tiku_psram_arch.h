/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_psram_arch.h - Boot-only P4 AP x16 PSRAM initialization.
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
/** @brief Return the last completed initialization stage for diagnostics. */
unsigned tiku_p4_psram_stage(void);
#endif
