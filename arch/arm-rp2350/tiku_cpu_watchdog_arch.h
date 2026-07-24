/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - RP2350 watchdog interface
 *
 * The RP2350 WDOG block counts a 24-bit microsecond field down to
 * zero from a reload value, then issues a system reset (or fires
 * an IRQ in interval mode — we don't expose interval mode in the
 * first port).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_WATCHDOG_ARCH_H_
#define TIKU_RP2350_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Mode + clock + interval typedefs (mirror MSP430 shape)                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Watchdog operating mode.
 *
 * WATCHDOG mode resets the system when the timer reaches zero.
 * INTERVAL mode fires an IRQ instead — not exposed in the first port.
 */
#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< System reset on timeout (default) */
    TIKU_WDT_MODE_INTERVAL = 1, /**< IRQ on timeout (not used on RP2350) */
} tiku_wdt_mode_t;
#endif

/**
 * @brief Watchdog clock source selector.
 *
 * Mirrors the MSP430 WDTSSEL encoding. On RP2350 the WDOG always
 * runs off the 1 µs tick block regardless of this field; the enum
 * is kept for source-level compatibility with the MSP430 HAL.
 */
#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0, /**< Sub-main clock (SMCLK / CLK_PERI) */
    TIKU_WDT_SRC_ACLK  = 1, /**< Auxiliary low-frequency clock (ACLK) */
} tiku_wdt_clk_t;
#endif

/**
 * @brief Watchdog interval/divider value.
 *
 * Same shape as MSP430's WDTIS encoding: a divider value. The
 * RP2350 implementation maps it to a microsecond timeout
 * (32768 -> ~1 s).
 */
#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
typedef uint16_t tiku_wdt_interval_t;
#endif

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Disable the watchdog timer.
 *
 * Clears the WDOG_CTRL.ENABLE bit. Safe to call from any context;
 * idempotent if the watchdog is already off.
 */
void tiku_cpu_rp2350_watchdog_off_arch(void);

/**
 * @brief Enable and configure the watchdog timer.
 *
 * Loads the microsecond timeout derived from @p isel, enables the
 * WDOG block, and performs the first kick. The @p src parameter is
 * accepted for API symmetry with the MSP430 HAL but is ignored on
 * RP2350 — the watchdog always uses the 1 µs tick source.
 *
 * @param src   Clock source (ignored on RP2350; present for HAL compat).
 * @param isel  Interval divider value — maps to microsecond timeout.
 */
void tiku_cpu_rp2350_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel);

/**
 * @brief Pause the watchdog counter without disabling it.
 *
 * Sets WDOG_CTRL.PAUSE_JTAG and PAUSE_DBG bits so the counter
 * freezes while the CPU is halted in a debugger. Calling this
 * outside a debug context stalls the counter until
 * tiku_cpu_rp2350_watchdog_resume_arch() is called.
 */
void tiku_cpu_rp2350_watchdog_pause_arch(void);

/**
 * @brief Resume a paused watchdog counter.
 *
 * Clears the PAUSE bits set by tiku_cpu_rp2350_watchdog_pause_arch().
 * If @p kick_on_resume is non-zero the watchdog is kicked immediately
 * on resume to avoid a stale timeout from the pause duration.
 *
 * @param kick_on_resume  Non-zero to kick on resume; 0 to leave as-is.
 */
void tiku_cpu_rp2350_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Kick (service) the watchdog to prevent a reset.
 *
 * Writes the service sequence to WDOG_LOAD, restarting the countdown
 * from the configured timeout. Must be called before the timeout
 * expires; typically invoked from the scheduler tick.
 */
void tiku_cpu_rp2350_watchdog_kick_arch(void);

#endif /* TIKU_RP2350_CPU_WATCHDOG_ARCH_H_ */
