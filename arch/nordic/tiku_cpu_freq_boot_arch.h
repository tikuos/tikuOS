/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - nRF54L clock/power boot bring-up
 *
 * The boot clock bring-up and the clock queries the CPU HAL routes to on
 * Nordic parts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_

/**
 * @brief Bring up the core clocks: set the core to the saved 64/128 MHz rate
 *        (the build default if none is saved), start the 32 MHz HFXO, then
 *        the cache and DC/DC.
 *
 * @note Call once at boot, before any high-frequency peripheral starts; the
 *       rate must not change on a running system.
 */
void tiku_cpu_boot_nordic_init(void);

/** @brief Peripheral clock in Hz: 16 MHz (PCLK16M), whatever the core rate. */
unsigned long tiku_cpu_nordic_smclk_get_hz(void);

/** @brief Non-zero if a clock source failed to start during boot. */
int tiku_cpu_nordic_clock_has_fault(void);

/** @brief No-op: the core frequency is set at boot only. */
void tiku_cpu_freq_nordic_init(unsigned int cpu_freq);

/** @brief Main clock (MCLK) in Hz, read from the live hardware. */
unsigned long tiku_cpu_nordic_clock_get_hz(void);

/** @brief Read a validated next-boot rate without initializing storage. */
unsigned long tiku_cpu_nordic_target_hz(void);
/**
 * @brief Persist a 64/128 MHz rate in Hz; the running clock is unchanged.
 * @return 0 when saved, -1 for any other rate or a failed write.
 */
int tiku_cpu_nordic_target_set(unsigned long hz);

/** @brief Auxiliary clock (ACLK) in Hz == 32.768 kHz LFCLK. */
unsigned long tiku_cpu_nordic_aclk_get_hz(void);

/** @brief Idle-mode hook: enter wait-for-interrupt (WFI). */
void tiku_cpu_boot_nordic_power_wfi_enter(void);

#endif /* TIKU_NORDIC_CPU_FREQ_BOOT_ARCH_H_ */
