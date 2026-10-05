/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - nRF54L watchdog (WDT30) arch interface.
 *
 * The watchdog arch interface the kernel watchdog layer calls, over WDT30: a
 * 32.768 kHz countdown whose timeout resets the system, reported by the
 * reset-reason decode as a watchdog reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CPU_WATCHDOG_ARCH_H_
#define TIKU_NORDIC_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

/* The kernel watchdog layer's mode, clock and interval types, guarded so
 * another arch header can define them first. */
#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/** @brief Watchdog timeout action. */
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< System reset on timeout (default) */
    TIKU_WDT_MODE_INTERVAL = 1, /**< IRQ on timeout (not used on nRF54L) */
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/** @brief Watchdog clock source; WDT30 always runs from the LFCLK. */
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0, /**< Sub-main clock (ignored; WDT30 uses 32 kHz) */
    TIKU_WDT_SRC_ACLK  = 1, /**< Auxiliary low-frequency clock (32.768 kHz) */
} tiku_wdt_clk_t;
#endif

#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
/** @brief Watchdog timeout in 32.768 kHz ticks (written to CRV). */
typedef uint16_t tiku_wdt_interval_t;
#endif

/** @brief Stop the watchdog (WDT30 supports TASKS_STOP). */
void tiku_cpu_nordic_watchdog_off_arch(void);

/**
 * @brief Configure + start the watchdog.
 *
 * @param src   Clock source (accepted for API symmetry; WDT30 runs off the
 *              32.768 kHz LFCLK regardless).
 * @param isel  Timeout in 32.768 kHz ticks (CRV): 32768 ~= 1 s.
 */
void tiku_cpu_nordic_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel);

/** @brief Pause the watchdog (stop the counter). */
void tiku_cpu_nordic_watchdog_pause_arch(void);

/** @brief Resume the watchdog; kick first if @p kick_on_resume is non-zero. */
void tiku_cpu_nordic_watchdog_resume_arch(int kick_on_resume);

/** @brief Kick (feed) the watchdog via the reload request key. */
void tiku_cpu_nordic_watchdog_kick_arch(void);

#endif /* TIKU_NORDIC_CPU_WATCHDOG_ARCH_H_ */
