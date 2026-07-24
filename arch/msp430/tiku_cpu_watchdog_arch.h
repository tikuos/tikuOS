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
 * Unlike tiku_cpu_msp430_watchdog_pause_arch(), this replaces the
 * whole WDTCTL low byte rather than preserving mode/clock/interval —
 * a later resume alone will not restore the previous configuration,
 * so re-enable via the _on_arch / _config_arch entry points.
 */
void tiku_cpu_msp430_watchdog_off_arch(void);

/**
 * @brief Pause (hold) the watchdog without losing its configuration.
 *
 * Sets WDTHOLD while preserving the rest of the WDTCTL low byte, so
 * mode, clock source and interval survive; the counter is left as it
 * is and keeps its value until resumed.  Held is not the same as off:
 * tiku_cpu_msp430_watchdog_resume_arch() restarts it without a full
 * reconfiguration.  Unlike resume/kick, this read-modify-write of
 * WDTCTL is not performed with interrupts disabled.
 */
void tiku_cpu_msp430_watchdog_pause_arch(void);

/**
 * @brief Resume a paused watchdog.
 *
 * Clears WDTHOLD while preserving every other WDTCTL low-byte bit
 * (mode, clock source, interval).  The read-modify-write runs with
 * interrupts disabled and the previous GIE state restored, so it is
 * safe against an ISR that also touches WDTCTL.
 *
 * @param kick_on_resume  0 resumes with the counter where pause left
 *                        it; non-zero also sets WDTCNTCL so the full
 *                        timeout interval is available again
 */
void tiku_cpu_msp430_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Kick (clear) the watchdog counter.
 *
 * Sets WDTCNTCL, restarting the timeout window from zero, while
 * preserving the rest of the WDTCTL low byte — including WDTHOLD, so
 * kicking a paused watchdog leaves it paused.  The read-modify-write
 * runs with interrupts disabled and the previous GIE state restored.
 * In watchdog mode this is what prevents a reset; in interval-timer
 * mode it just postpones the next WDTIFG.
 */
void tiku_cpu_msp430_watchdog_kick_arch(void);

/**
 * @brief Compose and write the whole WDTCTL low byte in one store.
 *
 * The primitive behind _on_arch and _interval_timer_on_arch: mode,
 * clock and interval are OR-ed together and written as a single
 * password-protected word, so any previous configuration is REPLACED,
 * not merged.
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
 * Convenience wrapper over tiku_cpu_msp430_watchdog_config_arch()
 * with WDTTMSEL = 0: the timer runs immediately with a cleared
 * counter, and reaching @p isel counts of @p src resets the device
 * unless tiku_cpu_msp430_watchdog_kick_arch() is called first.
 *
 * @param src   TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel  Interval divider, one of WDTIS__64, WDTIS__512,
 *              WDTIS__8192, WDTIS__32768 (or another device WDTIS__*)
 */
void tiku_cpu_msp430_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

/**
 * @brief Start the WDT in interval-timer mode.
 *
 * Same hardware, WDTTMSEL = 1: expiry sets WDTIFG and requests an
 * interrupt instead of resetting the device, giving a periodic tick.
 * The timer runs immediately with a cleared counter.  The interrupt
 * itself is left masked — the caller must enable it (SFRIE1 |= WDTIE)
 * and supply a WDT_VECTOR handler, otherwise expiry is silent.
 *
 * @param src   TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel  Interval divider, one of WDTIS__64, WDTIS__512,
 *              WDTIS__8192, WDTIS__32768 (or another device WDTIS__*)
 */
void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

#endif /* TIKU_CPU_WATCHDOG_ARCH_H_ */
