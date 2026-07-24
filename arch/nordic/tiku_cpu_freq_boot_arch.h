/*
 * Tiku Operating System v0.06
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

/** @brief Sub-main clock in Hz (== core clock, 128 MHz on this port). */
unsigned long tiku_cpu_nordic_smclk_get_hz(void);

/** @brief Non-zero if a clock source failed to start during boot. */
int tiku_cpu_nordic_clock_has_fault(void);

/** @brief Frequency init (no-op: the PLL is fixed at 128 MHz). */
void tiku_cpu_freq_nordic_init(unsigned int cpu_freq);

/** @brief Main clock (MCLK) in Hz == core clock (128 MHz). */
unsigned long tiku_cpu_nordic_clock_get_hz(void);

/** @brief Auxiliary clock (ACLK) in Hz == 32.768 kHz LFCLK. */
unsigned long tiku_cpu_nordic_aclk_get_hz(void);

/** @brief Idle-mode hook: enter wait-for-interrupt (WFI). */
void tiku_cpu_boot_nordic_power_wfi_enter(void);

#endif /* TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_ */
