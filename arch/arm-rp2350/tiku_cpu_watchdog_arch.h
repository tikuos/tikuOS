/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - RP2350 watchdog interface.
 *
 * The WDOG block counts a 24-bit microsecond field down from a reload value and
 * issues a system reset.  Interval mode is not exposed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_WATCHDOG_ARCH_H_
#define TIKU_RP2350_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* MODE, CLOCK AND INTERVAL TYPES                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Watchdog operating mode.
 *
 * WATCHDOG mode resets the system when the timer reaches zero.  INTERVAL
 * mode, an IRQ on timeout, is not implemented on this port.
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
 * The MSP430 WDTSSEL encoding, which the HAL passes through.  The RP2350
 * watchdog always counts the 1 us tick from the TICKS block, so this port
 * ignores it.
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
 * A divider of a 32768 Hz clock: the timeout is isel * 1 000 000 / 32768 us,
 * so 32768 gives 1 s.
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
 * Writes 0 to WD_CTRL, which stops the countdown and clears the pause bits.
 * Repeated calls have no further effect.
 *
 * @note Callable from any context.
 */
void tiku_cpu_rp2350_watchdog_off_arch(void);

/**
 * @brief Enable and configure the watchdog timer.
 *
 * Loads the microsecond timeout derived from @p isel, enables the watchdog and
 * reloads the count.  @p src is ignored: the watchdog always counts the 1 us
 * tick.
 *
 * @param src   Clock source (ignored on RP2350).
 * @param isel  Interval divider value — maps to microsecond timeout.
 */
void tiku_cpu_rp2350_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel);

/**
 * @brief Stop the watchdog countdown where it is.
 *
 * Clears WD_CTRL.ENABLE; the count holds its value until
 * tiku_cpu_rp2350_watchdog_resume_arch() sets ENABLE again.
 */
void tiku_cpu_rp2350_watchdog_pause_arch(void);

/**
 * @brief Resume a paused watchdog counter.
 *
 * Sets WD_CTRL.ENABLE again.  With @p kick_on_resume non-zero the count is
 * first reloaded with the full timeout, so time spent paused is not charged.
 *
 * @param kick_on_resume  Non-zero to kick on resume; 0 to leave as-is.
 */
void tiku_cpu_rp2350_watchdog_resume_arch(int kick_on_resume);

/**
 * @brief Kick (service) the watchdog to prevent a reset.
 *
 * Writes the armed timeout to WD_LOAD, restarting the countdown; does nothing
 * before tiku_cpu_rp2350_watchdog_on_arch() has armed it.
 *
 * @note Call it more often than the timeout, or the watchdog resets the chip.
 */
void tiku_cpu_rp2350_watchdog_kick_arch(void);

#endif /* TIKU_RP2350_CPU_WATCHDOG_ARCH_H_ */
