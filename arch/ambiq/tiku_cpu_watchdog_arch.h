/*
 * Tiku Operating System v0.05
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
/* Same shape as MSP430's WDTIS encoding: a divider value mapped to a
 * microsecond timeout. */
typedef uint16_t tiku_wdt_interval_t;
#endif

void tiku_cpu_ambiq_watchdog_off_arch(void);
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);
void tiku_cpu_ambiq_watchdog_pause_arch(void);
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume);
void tiku_cpu_ambiq_watchdog_kick_arch(void);

#endif /* TIKU_AMBIQ_CPU_WATCHDOG_ARCH_H_ */
