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

/* Spin rate of the delay loop in tiku_cpu_common.c, measured on a
 * NUCLEO-N657X0-Q. Five runs of identical images gave 132, 157, 162, 268 and
 * 390 M iterations/s -- a 3x spread with nothing changed but the reset.
 *
 * The port does not program the clock tree, and the rate the boot ROM leaves
 * behind is not reproducible across resets, so these delays are approximate --
 * good enough for a heartbeat or a settle, wrong for a protocol deadline.
 * Anything needing real time waits for a hardware time base (N6-3). */
#ifndef TIKU_STM32N6_SPIN_ITERS_PER_MS
#define TIKU_STM32N6_SPIN_ITERS_PER_MS  150000UL
#endif

/* CPU rate implied by the spin rate at about one cycle per iteration. Not read
 * from the clock tree and not stable across boots; treat it as a magnitude. */
#ifndef TIKU_STM32N6_CPU_HZ
#define TIKU_STM32N6_CPU_HZ     150000000UL
#endif

/**
 * @brief Prepare the clock state the rest of the port depends on.
 *
 * Ensures HSI is running and ready; the bus clock tree is left as the boot
 * ROM configured it.
 */
void tiku_cpu_boot_stm32n6_init(void);

/**
 * @brief CPU clock rate in Hz.
 *
 * @return TIKU_STM32N6_CPU_HZ, the assumed post-ROM rate
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
