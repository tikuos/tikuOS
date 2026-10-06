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
 * @brief Feed the watchdog and hand its feeding to the system tick until the
 *        next start; the IWDG cannot be turned off.
 *
 * @note The IWDG keeps counting; only a reset stops it.  Kicks after this
 *       still feed it, and so does every tick while the tick interrupt runs.
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

/**
 * @brief Reload the counter if the watchdog is turned off.
 *
 * Writes nothing while the watchdog is on or before its first start.
 *
 * @note Called by the LPTIM1 interrupt on every system tick.
 */
void tiku_cpu_stm32n6_watchdog_tick_arch(void);

#endif /* TIKU_STM32N6_CPU_WATCHDOG_ARCH_H_ */
