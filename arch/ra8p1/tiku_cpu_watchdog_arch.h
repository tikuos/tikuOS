/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - RA8P1 independent watchdog.
 *
 * The IWDT counts LOCO/2, so it outlives any system-clock change; once
 * refreshed it cannot be stopped or reconfigured until a reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU_WATCHDOG_ARCH_H_
#define TIKU_RA8P1_CPU_WATCHDOG_ARCH_H_

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

/** @brief Timeout selector, in IWDTCLK ticks (16.384 kHz). */
typedef uint16_t tiku_wdt_interval_t;

/**
 * @brief Record that the caller wants the watchdog off.
 *
 * @note Once refreshed the IWDT cannot be stopped or reconfigured until a
 *       reset.  This feeds the counter once and then ignores kicks, so
 *       unless the caller arms it again the part resets one interval later.
 */
void tiku_cpu_ra8p1_watchdog_off_arch(void);

/**
 * @brief Start the watchdog.
 *
 * @param src       Clock source request
 * @param interval  Timeout in IWDTCLK ticks (16.384 kHz)
 * @note IWDTCR accepts exactly one write between reset and the first refresh.
 *       A later call with a different interval CANNOT be honoured; it feeds
 *       the counter and leaves the period alone rather than pretending.
 */
void tiku_cpu_ra8p1_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t interval);

/** @brief Suspend watchdog counting across a long critical section. */
void tiku_cpu_ra8p1_watchdog_pause_arch(void);

/**
 * @brief Resume watchdog counting.
 *
 * @param kick_on_resume  Non-zero to reload the counter first
 */
void tiku_cpu_ra8p1_watchdog_resume_arch(int kick_on_resume);

/** @brief Reload the watchdog counter. */
void tiku_cpu_ra8p1_watchdog_kick_arch(void);

/**
 * @brief Report the period the IWDT is actually counting, in milliseconds.
 *
 * The hardware quantises a request to one of 24 (CKS, TOPS) pairs, so the
 * granted period is often not the requested one.
 *
 * @return Period in ms, or 0 when the watchdog has never been armed
 */
uint32_t tiku_cpu_ra8p1_watchdog_period_ms(void);

#endif /* TIKU_RA8P1_CPU_WATCHDOG_ARCH_H_ */
