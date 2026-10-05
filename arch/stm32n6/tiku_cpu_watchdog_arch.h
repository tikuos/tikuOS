/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - STM32N6 independent watchdog.
 *
 * The IWDG counts off the LSI and keeps its rate through system-clock
 * changes; once started, only a reset stops it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CPU_WATCHDOG_ARCH_H_
#define TIKU_STM32N6_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/** @brief What a timeout does. */
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< Reset the system on timeout */
    TIKU_WDT_MODE_INTERVAL = 1, /**< Interrupt on timeout; unsupported here */
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/** @brief Clock feeding the watchdog counter. */
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0, /**< Peripheral clock */
    TIKU_WDT_SRC_ACLK  = 1, /**< Low-frequency clock */
} tiku_wdt_clk_t;
#endif

/** @brief Watchdog timeout, in LSI ticks on this port. */
typedef uint16_t tiku_wdt_interval_t;

/**
 * @brief Record the watchdog as off and feed it one last time.
 *
 * @note The IWDG keeps counting; only a reset stops it.  A caller that stops
 *       kicking after this is still reset once the interval expires.
 */
void tiku_cpu_stm32n6_watchdog_off_arch(void);

/**
 * @brief Start the watchdog.
 *
 * @param src       Recorded only: the IWDG always counts off the LSI
 * @param interval  Timeout in LSI ticks (32 kHz); 0 counts as 1
 * @note Once armed, the IWDG runs until the next reset; off and pause feed
 *       the counter and cannot halt it.
 */
void tiku_cpu_stm32n6_watchdog_on_arch(tiku_wdt_clk_t src,
                                       tiku_wdt_interval_t interval);

/**
 * @brief Grant a long critical section a fresh full interval to run in.
 *
 * @note The counter cannot be halted, so a section longer than one interval
 *       is reset even while paused.
 */
void tiku_cpu_stm32n6_watchdog_pause_arch(void);

/**
 * @brief Clear the paused state; the counter never stopped.
 *
 * @param kick_on_resume  Non-zero to reload the counter
 */
void tiku_cpu_stm32n6_watchdog_resume_arch(int kick_on_resume);

/** @brief Reload the watchdog counter. */
void tiku_cpu_stm32n6_watchdog_kick_arch(void);

#endif /* TIKU_STM32N6_CPU_WATCHDOG_ARCH_H_ */
