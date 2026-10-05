/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - Ambiq hardware watchdog interface.
 *
 * The WDT runs from an LFRC tap and resets the chip on timeout; it has no
 * interval mode here.  Implemented in tiku_cpu_watchdog_arch.c (Apollo510)
 * and tiku_cpu_watchdog_apollo4l.c (Apollo4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_
#define TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/**
 * @brief Watchdog operating mode (portable type).
 *
 * WATCHDOG resets the system on timeout; INTERVAL fires a periodic
 * interrupt.  This port runs the WDT in WATCHDOG mode only.
 */
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< Reset system on timeout. */
    TIKU_WDT_MODE_INTERVAL = 1, /**< Generate interrupt on timeout. */
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/**
 * @brief Watchdog clock source selector (portable type).
 *
 * This port ignores it: the WDT always runs from an LFRC tap, which keeps
 * counting through deep sleep.
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
 * A divider on a nominal 32768 Hz clock, so the timeout is isel / 32768 s;
 * the arch implementation picks the LFRC tap and compare value that hold it.
 */
typedef uint16_t tiku_wdt_interval_t;
#endif

/**
 * @brief Stop the watchdog counter and disable its reset.
 *
 * The WDT is off out of reset; calling this at boot leaves it off whatever
 * state the boot code left it in.
 */
void tiku_cpu_ambiq_watchdog_off_arch(void);

/**
 * @brief Start the watchdog with a timeout of isel / 32768 s (at most 2 s).
 *
 * Arms the reset path (WDT.RESEN and RSTGEN.WDREN) and starts counting from
 * zero.  The timeout is at least one 128 Hz LFRC tick (~7.8 ms).
 *
 * @param src   Clock source (ignored; the WDT runs from the LFRC).
 * @param isel  Timeout interval selector (divider on 32768 Hz).
 */
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel);

/**
 * @brief Stop the watchdog counter, keeping its timeout setting.
 *
 * For long operations, such as NVM programming, that would outlast the
 * timeout.
 */
void tiku_cpu_ambiq_watchdog_pause_arch(void);

/**
 * @brief Restart the watchdog counter after a pause.
 *
 * @param kick_on_resume  Non-zero to restart the count from zero first.
 */
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Restart the watchdog count from zero (kick).
 *
 * @note While the WDT is running, call it more often than the timeout.
 */
void tiku_cpu_ambiq_watchdog_kick_arch(void);

#endif /* TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_ */
