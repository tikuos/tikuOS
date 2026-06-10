/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.h - Apollo 510 system-tick (Cortex-M SysTick)
 *
 * Mirrors arch/arm-rp2350/tiku_timer_arch.h. The system clock runs at
 * TIKU_CLOCK_ARCH_SECOND ticks/second (128 Hz by default). The SysTick
 * reload is derived from TIKU_MAIN_CPU_HZ (96 MHz / 128 = 750000, well
 * within SysTick's 24-bit reload limit).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_TIMER_ARCH_H_
#define TIKU_AMBIQ_TIMER_ARCH_H_

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

/* SysTick is clocked from the core clock; reload = core_hz / TICK_HZ.
 * TIKU_MAIN_CPU_HZ tracks MAIN_CPU_FREQ so the tick stays at
 * TIKU_CLOCK_ARCH_SECOND Hz. */
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

#endif /* TIKU_AMBIQ_TIMER_ARCH_H_ */
