/*
 * Tiku Operating System v0.04
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

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0,
    TIKU_WDT_MODE_INTERVAL = 1,
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0,
    TIKU_WDT_SRC_ACLK  = 1,
} tiku_wdt_clk_t;
#endif

#ifndef TIKU_WDT_INTERVAL_T_DEFINED
#define TIKU_WDT_INTERVAL_T_DEFINED
/* Same shape as MSP430's WDTIS encoding: just a divider value. The
 * implementation maps it to a microsecond timeout (32768 -> ~1 s). */
typedef uint16_t tiku_wdt_interval_t;
#endif

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_cpu_rp2350_watchdog_off_arch(void);
void tiku_cpu_rp2350_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);
void tiku_cpu_rp2350_watchdog_pause_arch(void);
void tiku_cpu_rp2350_watchdog_resume_arch(int kick_on_resume);
void tiku_cpu_rp2350_watchdog_kick_arch(void);

#endif /* TIKU_RP2350_CPU_WATCHDOG_ARCH_H_ */
