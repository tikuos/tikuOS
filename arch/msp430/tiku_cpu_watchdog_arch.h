/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - MSP430FR5969 CPU watchdog timer configuration
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
void tiku_cpu_msp430_watchdog_off_arch(void);
void tiku_cpu_msp430_watchdog_pause_arch(void);
void tiku_cpu_msp430_watchdog_resume_arch(int kick_on_resume);
void tiku_cpu_msp430_watchdog_kick_arch(void);
void tiku_cpu_msp430_watchdog_config_arch(tiku_wdt_mode_t mode,
                              tiku_wdt_clk_t src,
                              tiku_wdt_interval_t isel,
                              int start_held,
                              int kick_on_start);   

void tiku_cpu_msp430_watchdog_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);
void tiku_cpu_msp430_watchdog_interval_timer_on_arch(tiku_wdt_clk_t src, tiku_wdt_interval_t isel);

#endif /* TIKU_CPU_WATCHDOG_ARCH_H_ */
