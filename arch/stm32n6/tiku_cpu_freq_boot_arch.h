/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - STM32N6 boot-time clock state.
 *
 * The port inherits the clock tree the boot ROM leaves running and does not
 * program the PLL, so the reported CPU rate is the assumption below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_

/* Fallback CPU rate, used only until LPTIM1 runs and the real rate can be
 * measured. Measured against the LPTIM reference the inherited clock reads a
 * consistent 128 MHz (2x HSI) across boots; the port still measures rather
 * than asserts, because the ROM owns that choice. 600 MHz is the part's
 * maximum and needs PLL1 plus voltage scaling, which the port does not yet do. */
#ifndef TIKU_STM32N6_CPU_HZ
#define TIKU_STM32N6_CPU_HZ     128000000UL
#endif

/* Spin rate fallback for delays before the first measurement lands. */
#ifndef TIKU_STM32N6_SPIN_ITERS_PER_MS
#define TIKU_STM32N6_SPIN_ITERS_PER_MS  (TIKU_STM32N6_CPU_HZ / 1000UL)
#endif

/**
 * @brief Prepare the clock state the rest of the port depends on.
 *
 * Ensures HSI is running and ready; the bus clock tree is left as the boot
 * ROM configured it.
 */
void tiku_cpu_boot_stm32n6_init(void);

/**
 * @brief CPU clock rate in Hz, measured against LPTIM1.
 *
 * Measured once per boot on first call after the tick starts; before that the
 * compile-time fallback is reported.
 *
 * @return Measured rate, or TIKU_STM32N6_CPU_HZ until LPTIM1 runs
 */
unsigned long tiku_cpu_stm32n6_clock_get_hz(void);

/**
 * @brief Peripheral clock rate in Hz.
 *
 * @return The HSI rate actually reaching the peripherals, HSIDIV applied
 */
unsigned long tiku_cpu_stm32n6_smclk_get_hz(void);

/**
 * @brief Report whether the oscillator the port depends on failed to start.
 *
 * @return 1 when HSI is not ready, 0 when the clock is usable
 */
int tiku_cpu_stm32n6_clock_has_fault(void);

#endif /* TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_ */
