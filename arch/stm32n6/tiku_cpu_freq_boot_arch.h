/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - STM32N6 clock tree: state, measurement, control.
 *
 * PLL1 feeds IC1 for the core and IC2/IC6/IC11 for the buses, so the core
 * rate changes on its own while the buses stay at ST's rates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_

/**
 * @brief Core rate the delay loops assume until the spin rate is measured.
 *
 * 150 MHz is the boot ROM's hand-over rate (PLL1 1200 MHz over an IC1 divider
 * of 8) and the default MAIN_CPU_FREQ, which boot sets again.
 */
#ifndef TIKU_STM32N6_CPU_HZ
#define TIKU_STM32N6_CPU_HZ     150000000UL
#endif

/** @brief Spin iterations per millisecond before the first measurement. */
#ifndef TIKU_STM32N6_SPIN_ITERS_PER_MS
#define TIKU_STM32N6_SPIN_ITERS_PER_MS  (TIKU_STM32N6_CPU_HZ / 1000UL)
#endif

/**
 * @brief Turn HSI on and wait for it to be ready.
 *
 * The bus clock tree is left as the boot ROM configured it.
 *
 * @note Returns after 1000000 polls without HSIRDY; then
 *       tiku_cpu_stm32n6_clock_has_fault() returns 1.
 */
void tiku_cpu_boot_stm32n6_init(void);

/**
 * @brief CPU clock rate in Hz, measured against LPTIM1.
 *
 * Measured once per clock configuration after the tick starts; before that
 * the rate is decoded from the RCC clock tree.
 *
 * @return Measured rate, or the clock-tree rate until LPTIM1 runs
 */
unsigned long tiku_cpu_stm32n6_clock_get_hz(void);

/**
 * @brief Peripheral clock rate in Hz.
 *
 * @return HSI after HSIDIV, in Hz
 */
unsigned long tiku_cpu_stm32n6_smclk_get_hz(void);

/**
 * @brief Delay-loop iterations per millisecond, measured on this core.
 *
 * @return Measured loop rate, or TIKU_STM32N6_SPIN_ITERS_PER_MS before the
 *         first measurement
 */
unsigned long tiku_cpu_stm32n6_spin_per_ms(void);

/**
 * @brief Report whether HSI, the clock of the tick and the console, is off.
 *
 * @return 1 when HSI is not ready, 0 when the clock is usable
 */
int tiku_cpu_stm32n6_clock_has_fault(void);

/*---------------------------------------------------------------------------*/
/* CORE FREQUENCY                                                            */
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
 * Accepts 64 (the core on HSI directly), exact divisors of 1200 from 5 to 600,
 * and 800, which also raises the core rail through the board's SMPS.
 *
 * @param mhz  Requested core frequency in MHz
 * @note Makes no change for an unsupported rate or when HSI does not start;
 *       when PLL1 does not lock, the core and buses stay on HSI.
 */
void tiku_cpu_freq_stm32n6_init(unsigned int mhz);

/**
 * @brief Report whether a frequency is one this port can select.
 *
 * @param mhz  Frequency in MHz
 * @return 1 when supported, 0 otherwise
 */
int tiku_cpu_freq_stm32n6_supported(unsigned int mhz);

/**
 * @brief Report whether @p hz can be set at boot by changing IC1 alone.
 *
 * The first call captures the PLL1 rate and voltage range.  A rate qualifies
 * from 100 MHz to 600 MHz (800 MHz with VOS high) when it divides that PLL1
 * rate by a whole number up to 256.
 *
 * @param hz  Core rate in Hz
 * @return 1 when the rate qualifies, 0 otherwise
 * @note Nothing qualifies when PLL1 was unlocked, fed by MSI or HSE, or
 *       fractional at the first call.
 */
int tiku_cpu_stm32n6_boot_rate_supported(unsigned long hz);

/**
 * @brief Set the core rate at boot by changing the IC1 divider alone.
 *
 * PLL1, the bus clocks, the XSPI clock and the core voltage do not change.
 *
 * @param hz  Core rate in Hz
 * @note Does nothing unless tiku_cpu_stm32n6_boot_rate_supported() accepts
 *       @p hz.
 */
void tiku_cpu_stm32n6_boot_divide(unsigned long hz);

#endif /* TIKU_STM32N6_CPU_FREQ_BOOT_ARCH_H_ */
