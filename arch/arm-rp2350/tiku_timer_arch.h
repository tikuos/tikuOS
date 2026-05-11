/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - RP2350 system-tick (Cortex-M SysTick)
 *
 * The system clock runs at TIKU_CLOCK_ARCH_SECOND ticks per second.
 * Default 128 Hz to match the MSP430 port — gives ~7.8 ms resolution
 * which is plenty for the protothread scheduler and far below the
 * 24-bit SysTick reload limit at 150 MHz CPU clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_TIMER_ARCH_H_
#define TIKU_RP2350_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128   /* must be a power of 2 */
#endif

#define TIKU_CLOCK_ARCH_SECOND  TIKU_CLOCK_ARCH_CONF_SECOND
/* SysTick uses CPU clock; reload = clk_sys / TICK_HZ. TIKU_MAIN_CPU_HZ
 * tracks MAIN_CPU_FREQ so the system tick stays at TIKU_CLOCK_ARCH_SECOND
 * Hz across all supported clk_sys frequencies (12 / 48 / 100 / 125 /
 * 133 / 150 MHz). */
#define TIKU_CLOCK_ARCH_INTERVAL  (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

void                   tiku_clock_arch_init(void);
tiku_clock_arch_time_t tiku_clock_arch_time(void);
unsigned long          tiku_clock_arch_seconds(void);
void                   tiku_clock_arch_set_seconds(unsigned long sec);
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);
void                   tiku_clock_arch_delay(unsigned int us);
unsigned short         tiku_clock_arch_fine(void);
int                    tiku_clock_arch_fine_max(void);

#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_RP2350_TIMER_ARCH_H_ */
