/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - nRF54L clock/power boot bring-up
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_

/**
 * @brief Bring up the core clocks: start the 32 MHz HFXO (needed for accurate
 *        UARTE / radio timing).  Delays use SysTick and need no setup here.
 *
 * The core runs at 128 MHz on this DK; this does not change the core
 * frequency.
 */
void tiku_cpu_boot_nordic_init(void);

/** @brief Current core clock in Hz (128 MHz on this port). */
unsigned long tiku_cpu_nordic_smclk_get_hz(void);

/** @brief Non-zero if a clock source failed to start during boot. */
int tiku_cpu_nordic_clock_has_fault(void);

#endif /* TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_ */
