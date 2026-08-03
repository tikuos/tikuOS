/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - STM32N6 clock tree: state, measurement, control.
 *
 * PLL1 feeds IC1 for the core and IC2/IC6/IC11 for the buses, so the core rate
 * moves from 10 MHz to 800 MHz while the buses stay at ST's proven rates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_

/* Fallback core rate, used only until LPTIM1 runs and the DWT cycle counter
 * can measure the real one. The boot ROM hands over at 150 MHz (PLL1 1200 MHz
 * over an IC1 divider of 8), which is what this port then reprograms to the
 * same value at boot so the tree is owned rather than inherited. */
#ifndef TIKU_STM32N6_CPU_HZ
#define TIKU_STM32N6_CPU_HZ     150000000UL
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
 * @brief Delay-loop iterations per millisecond, measured on this core.
 *
 * @return Measured loop rate, or the compile-time fallback before first measure
 */
unsigned long tiku_cpu_stm32n6_spin_per_ms(void);

/**
 * @brief Report whether the oscillator the port depends on failed to start.
 *
 * @return 1 when HSI is not ready, 0 when the clock is usable
 */
int tiku_cpu_stm32n6_clock_has_fault(void);

/*---------------------------------------------------------------------------*/
/* Core frequency                                                            */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/** @brief Live clock-tree state, as read back from RCC and PWR. */
typedef struct {
    uint8_t       cpu_src;      /**< CPUSWS: 0 HSI, 1 MSI, 2 HSE, 3 IC1 */
    uint8_t       sys_src;      /**< SYSSWS, same encoding */
    uint8_t       pll1_on;      /**< PLL1 enabled */
    uint8_t       pll1_ready;   /**< PLL1 locked */
    uint8_t       pll1_src;     /**< PLL1SEL: 0 HSI, 1 MSI, 2 HSE */
    uint8_t       pll1_p1;      /**< PLL1 post-divider 1 */
    uint8_t       pll1_p2;      /**< PLL1 post-divider 2 */
    uint8_t       vos_high;     /**< VOS range 0 (high frequency) selected */
    uint16_t      pll1_m;       /**< PLL1 reference pre-divider */
    uint16_t      pll1_n;       /**< PLL1 multiplier */
    uint32_t      pll1_frac;    /**< PLL1 fractional multiplier */
    uint16_t      ic1_div;      /**< CPU divider */
    uint16_t      ic2_div;      /**< bus divider */
    uint8_t       ic1_sel;      /**< which PLL feeds IC1 */
    unsigned long pll1_hz;      /**< PLL1 output, computed */
    unsigned long cpu_hz;       /**< CPU rate implied by the tree, computed */
    unsigned long ahb_div;      /**< AHB prescaler, as a divisor */
} tiku_stm32n6_clock_t;

/**
 * @brief Read the live clock tree.
 *
 * @param out  Receives the current configuration; NULL is ignored
 */
void tiku_cpu_stm32n6_clock_probe(tiku_stm32n6_clock_t *out);

/**
 * @brief Set the core frequency.
 *
 * Accepts 64 (HSI direct, PLL bypassed), any exact divisor of 1200 up to 600,
 * and 800 which additionally raises the core rail through the board's SMPS.
 *
 * @param mhz  Requested core frequency in MHz
 */
void tiku_cpu_freq_stm32n6_init(unsigned int mhz);

/**
 * @brief Report whether a frequency is one this port can select.
 *
 * @param mhz  Frequency in MHz
 * @return 1 when supported, 0 otherwise
 */
int tiku_cpu_freq_stm32n6_supported(unsigned int mhz);

#endif /* TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_ */
