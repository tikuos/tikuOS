/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_watchdog_arch.h - STM32F411RE watchdog interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_CPU_WATCHDOG_ARCH_H_
#define TIKU_STM32F411_CPU_WATCHDOG_ARCH_H_

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
typedef uint16_t tiku_wdt_interval_t;
#endif

void tiku_cpu_stm32f411_watchdog_off_arch(void);
void tiku_cpu_stm32f411_watchdog_on_arch(tiku_wdt_clk_t src,
                                         tiku_wdt_interval_t isel);
void tiku_cpu_stm32f411_watchdog_pause_arch(void);
void tiku_cpu_stm32f411_watchdog_resume_arch(int kick_on_resume);
void tiku_cpu_stm32f411_watchdog_kick_arch(void);

#endif /* TIKU_STM32F411_CPU_WATCHDOG_ARCH_H_ */
