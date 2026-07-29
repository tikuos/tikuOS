/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - MSP430 CPU watchdog timer configuration
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU_WATCHDOG_ARCH_H_
#define TIKU_CPU_WATCHDOG_ARCH_H_

#include <msp430.h>
#include <stdint.h>

/* Add missing WDTIS constants if not defined in MSP430 headers */
#ifndef WDTIS__64
#define WDTIS__64       (0x0000)  /* WDT - Timer Interval Select: /64 */
#endif
#ifndef WDTIS__512
#define WDTIS__512      (0x0001)  /* WDT - Timer Interval Select: /512 */
#endif
#ifndef WDTIS__8192
#define WDTIS__8192     (0x0002)  /* WDT - Timer Interval Select: /8192 */
#endif
#ifndef WDTIS__32768
#define WDTIS__32768    (0x0003)  /* WDT - Timer Interval Select: /32768 */
#endif

/* Mode */
#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
enum tiku_wdt_mode {
    TIKU_WDT_MODE_WATCHDOG = 0,          /* reset on timeout (WDTTMSEL=0) */
    TIKU_WDT_MODE_INTERVAL = WDTTMSEL    /* periodic interrupt (WDTTMSEL=1) */
};
typedef enum tiku_wdt_mode tiku_wdt_mode_t;
#endif

/* Clock source */
#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
enum tiku_wdt_clk {
    TIKU_WDT_SRC_SMCLK = WDTSSEL__SMCLK, /* usually 0 */
    TIKU_WDT_SRC_ACLK  = WDTSSEL__ACLK
};
typedef enum tiku_wdt_clk tiku_wdt_clk_t;
#endif

/* Interval: pass one of the device header macros, e.g.
   WDTIS__64, WDTIS__512, WDTIS__8192, WDTIS__32768, etc. */
#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
typedef uint16_t tiku_wdt_interval_t;
#endif

/* Watchdog control */

/**
 * @brief Stop the watchdog completely (WDTPW | WDTHOLD).
 *
 * Replaces the whole control low byte rather than preserving mode, clock and
 * interval, so a later resume alone will not restore the configuration --
 * re-enable through the _on_arch or _config_arch entry points.
 */
void tiku_cpu_msp430_watchdog_off_arch(void);

/**
 * @brief Pause (hold) the watchdog without losing its configuration.
 *
 * Sets WDTHOLD but preserves mode, clock and interval, and leaves the counter
 * where it is.  Held is not off: resume restarts it without reconfiguring.
 * Unlike resume and kick, this read-modify-write is not interrupt-protected.
 */
void tiku_cpu_msp430_watchdog_pause_arch(void);

/**
 * @brief Resume a paused watchdog.
 *
 * Clears WDTHOLD while preserving every other control bit.  The
 * read-modify-write runs with interrupts disabled and the prior state restored,
 * so it is safe against an ISR that also touches the register.
 *
 * @param kick_on_resume  0 resumes with the counter where pause left
 *                        it; non-zero also sets WDTCNTCL so the full
 *                        timeout interval is available again
 */
void tiku_cpu_msp430_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Kick (clear) the watchdog counter.
 *
 * Restarts the timeout window while preserving the rest of the control byte,
 * WDTHOLD included -- so kicking a paused watchdog leaves it paused.  In
 * watchdog mode this is what prevents a reset; in interval mode it defers the flag.
 */
void tiku_cpu_msp430_watchdog_kick_arch(void);

/**
 * @brief Compose and write the whole control low byte in one store.
 *
 * The primitive behind the two _on_arch entry points: mode, clock and interval
 * are OR-ed and written as one password-protected word, so any previous
 * configuration is REPLACED rather than merged.
 *
 * @param mode           TIKU_WDT_MODE_WATCHDOG (expiry resets the
 *                       device) or TIKU_WDT_MODE_INTERVAL (expiry
 *                       sets WDTIFG and requests an interrupt)
 * @param src            TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel           One of the WDTIS__* interval constants above
 * @param start_held     Non-zero leaves WDTHOLD set (configured but
 *                       paused); zero starts it running immediately
 * @param kick_on_start  Non-zero also sets WDTCNTCL so the first
 *                       timeout window starts from zero
 */
void tiku_cpu_msp430_watchdog_config_arch(tiku_wdt_mode_t mode,
                              tiku_wdt_clk_t src,
                              tiku_wdt_interval_t isel,
                              int start_held,
                              int kick_on_start);

/**
 * @brief Start the WDT in watchdog (reset) mode.
 *
 * A wrapper over the config primitive with the timer-mode bit clear: it runs
 * immediately from a cleared counter, and reaching the interval resets the
 * device unless a kick arrives first.
 *
 * @param src   TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel  Interval divider, one of WDTIS__64, WDTIS__512,
 *              WDTIS__8192, WDTIS__32768 (or another device WDTIS__*)
 */
void tiku_cpu_msp430_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

/**
 * @brief Start the WDT in interval-timer mode.
 *
 * Same hardware with the timer-mode bit set: expiry raises the flag and
 * requests an interrupt instead of resetting.  The interrupt is left masked, so
 * the caller must enable it and supply a handler or expiry is silent.
 *
 * @param src   TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel  Interval divider, one of WDTIS__64, WDTIS__512,
 *              WDTIS__8192, WDTIS__32768 (or another device WDTIS__*)
 */
void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

#endif /* TIKU_CPU_WATCHDOG_ARCH_H_ */
