/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - MSP430 CPU watchdog timer configuration
 *
 * WDT_A control for the watchdog HAL: stop, hold, resume, kick and configure,
 * each a password-carrying write to WDTCTL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU_WATCHDOG_ARCH_H_
#define TIKU_CPU_WATCHDOG_ARCH_H_

#include <msp430.h>
#include <stdint.h>

/**
 * @name WDTIS interval selects, defined here when <msp430.h> lacks them
 * The toolchain headers for the supported parts define WDTIS__64,
 * WDTIS__512 and WDTIS__8192 but name /32768 WDTIS__32K, so WDTIS__32768
 * always takes the value below.
 * @{
 */
#ifndef WDTIS__64
#define WDTIS__64       (0x0000)  /**< WDT - Timer Interval Select: /64 */
#endif
#ifndef WDTIS__512
#define WDTIS__512      (0x0001)  /**< WDT - Timer Interval Select: /512 */
#endif
#ifndef WDTIS__8192
#define WDTIS__8192     (0x0002)  /**< WDT - Timer Interval Select: /8192 */
#endif
#ifndef WDTIS__32768
#define WDTIS__32768    (0x0003)  /**< WDT - Timer Interval Select: /32768 */
#endif
/** @} */

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/** @brief WDT mode: the WDTTMSEL bit of WDTCTL. */
enum tiku_wdt_mode {
    TIKU_WDT_MODE_WATCHDOG = 0,          /**< reset on timeout (WDTTMSEL=0) */
    TIKU_WDT_MODE_INTERVAL = WDTTMSEL    /**< periodic interrupt (WDTTMSEL=1) */
};
/** @brief WDT mode. */
typedef enum tiku_wdt_mode tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/** @brief WDT clock source: the WDTSSEL field of WDTCTL. */
enum tiku_wdt_clk {
    TIKU_WDT_SRC_SMCLK = WDTSSEL__SMCLK, /**< SMCLK (WDTSSEL = 0) */
    TIKU_WDT_SRC_ACLK  = WDTSSEL__ACLK   /**< ACLK */
};
/** @brief WDT clock source. */
typedef enum tiku_wdt_clk tiku_wdt_clk_t;
#endif

#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
/**
 * @brief WDTIS interval select as written to WDTCTL: WDTIS__64, WDTIS__512,
 *        WDTIS__8192, WDTIS__32768 or another WDTIS__* of the device header.
 */
typedef uint16_t tiku_wdt_interval_t;
#endif

/**
 * @brief Stop the watchdog completely (WDTPW | WDTHOLD).
 *
 * The write clears the rest of the control byte (mode, clock, interval), so
 * resume alone does not bring the configuration back; _on_arch or
 * _config_arch does.
 */
void tiku_cpu_msp430_watchdog_off_arch(void);

/**
 * @brief Pause (hold) the watchdog without losing its configuration.
 *
 * Sets WDTHOLD but preserves mode, clock and interval, and leaves the counter
 * where it is.  Held is not off: resume restarts it without reconfiguring.
 *
 * @note The read-modify-write runs with interrupts enabled, so an ISR that
 *       writes WDTCTL in between loses its change.
 */
void tiku_cpu_msp430_watchdog_pause_arch(void);

/**
 * @brief Resume a paused watchdog.
 *
 * Clears WDTHOLD while preserving every other control bit.  The
 * read-modify-write runs with interrupts disabled and then restores GIE, so
 * an ISR that writes WDTCTL cannot interleave with it.
 *
 * @param kick_on_resume  0 resumes with the counter where pause left
 *                        it; non-zero also sets WDTCNTCL so the full
 *                        timeout interval is available again
 */
void tiku_cpu_msp430_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Kick (clear) the watchdog counter.
 *
 * Sets WDTCNTCL and keeps the rest of the control byte, WDTHOLD included, so
 * a paused watchdog stays paused.  In watchdog mode a kick before expiry
 * prevents the reset; in interval mode it postpones WDTIFG.
 */
void tiku_cpu_msp430_watchdog_kick_arch(void);

/**
 * @brief Compose and write the whole control low byte in one store.
 *
 * The primitive behind the two _on_arch entry points: mode, clock and interval
 * are OR-ed and written as one password-protected word, so any previous
 * configuration is replaced, not merged.
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
 * Same hardware with the timer-mode bit set: expiry sets WDTIFG instead of
 * resetting the device.
 *
 * @note WDTIE in SFRIE1 is not touched: without the caller setting it and
 *       providing the WDT ISR, an expiry raises no interrupt.
 *
 * @param src   TIKU_WDT_SRC_SMCLK or TIKU_WDT_SRC_ACLK
 * @param isel  Interval divider, one of WDTIS__64, WDTIS__512,
 *              WDTIS__8192, WDTIS__32768 (or another device WDTIS__*)
 */
void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

#endif /* TIKU_CPU_WATCHDOG_ARCH_H_ */
