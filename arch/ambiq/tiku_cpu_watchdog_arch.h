/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - Apollo 510 watchdog interface
 *
 * Mirrors arch/arm-rp2350/tiku_cpu_watchdog_arch.h. At this milestone
 * only _off() is real (the WDT is disabled out of reset); the rest are
 * placeholders pending an am_hal_wdt-backed implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_
#define TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/**
 * @brief Watchdog operating mode.
 *
 * Selects whether the WDT generates a system reset on timeout
 * (WATCHDOG mode) or fires a periodic interrupt (INTERVAL mode).
 */
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< Reset system on timeout. */
    TIKU_WDT_MODE_INTERVAL = 1, /**< Generate interrupt on timeout. */
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/**
 * @brief Watchdog clock source selector.
 *
 * Chooses the clock that drives the WDT counter. SMCLK is the
 * sub-main (peripheral) clock; ACLK is the low-frequency auxiliary
 * clock (~32 kHz), which extends the maximum timeout and keeps the
 * WDT running during deep-sleep.
 */
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0, /**< Sub-main clock (higher frequency). */
    TIKU_WDT_SRC_ACLK  = 1, /**< Auxiliary low-frequency clock. */
} tiku_wdt_clk_t;
#endif

#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
/**
 * @brief Watchdog timeout interval selector.
 *
 * Encodes the WDT prescaler divider value, matching the shape of the
 * MSP430 WDTIS field. The concrete mapping to microseconds depends on
 * the selected clock source and is defined in the arch implementation.
 */
typedef uint16_t tiku_wdt_interval_t;
#endif

/**
 * @brief Disable the Apollo510 watchdog timer.
 *
 * The WDT is disabled by default out of reset (SBL leaves it off).
 * Call this early in boot to guarantee the WDT stays off if the SBL
 * leaves it in an unknown state on future silicon revisions.
 */
void tiku_cpu_ambiq_watchdog_off_arch(void);

/**
 * @brief Enable and configure the Apollo510 watchdog timer.
 *
 * Placeholder at this milestone — a full am_hal_wdt-backed
 * implementation is deferred to a future peripheral pass.
 *
 * @param src   Clock source for the WDT counter.
 * @param isel  Timeout interval selector (prescaler divider value).
 */
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel);

/**
 * @brief Pause (temporarily disable) the watchdog counter.
 *
 * Placeholder at this milestone. Intended for use around time-consuming
 * NVM operations that would otherwise trigger a false timeout.
 */
void tiku_cpu_ambiq_watchdog_pause_arch(void);

/**
 * @brief Resume the watchdog counter after a pause.
 *
 * Placeholder at this milestone.
 *
 * @param kick_on_resume  Non-zero to service (kick) the WDT immediately
 *                        on resume, resetting the timeout counter.
 */
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Service (kick) the watchdog to prevent a timeout reset.
 *
 * Must be called periodically — faster than the configured WDT timeout
 * interval — when the WDT is enabled. Placeholder at this milestone.
 */
void tiku_cpu_ambiq_watchdog_kick_arch(void);

#endif /* TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_ */
