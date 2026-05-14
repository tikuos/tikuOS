/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_timer_arch.h - STM32F411RE system tick (Cortex-M SysTick)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_TIMER_ARCH_H_
#define TIKU_STM32F411_TIMER_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Type definitions                                                          */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_TIME_T_DEFINED
typedef unsigned long tiku_clock_arch_time_t;
#define TIKU_CLOCK_ARCH_TIME_T_DEFINED
#endif

typedef unsigned int tiku_clock_arch_counter_t;

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_CONF_SECOND 128
#endif

#define TIKU_CLOCK_ARCH_SECOND   TIKU_CLOCK_ARCH_CONF_SECOND
#define TIKU_CLOCK_ARCH_INTERVAL (TIKU_MAIN_CPU_HZ / TIKU_CLOCK_ARCH_SECOND)

/*---------------------------------------------------------------------------*/
/* HAL entry points                                                          */
/*---------------------------------------------------------------------------*/

void                   tiku_clock_arch_init(void);
tiku_clock_arch_time_t tiku_clock_arch_time(void);
unsigned long          tiku_clock_arch_seconds(void);
void                   tiku_clock_arch_set_seconds(unsigned long sec);
void                   tiku_clock_arch_wait(tiku_clock_arch_time_t t);
void                   tiku_clock_arch_delay(unsigned int us);
unsigned short         tiku_clock_arch_fine(void);
int                    tiku_clock_arch_fine_max(void);
unsigned char          tiku_clock_arch_fault(void);

#define TIKU_CLOCK_ARCH_MS_TO_TICKS(ms) \
    ((tiku_clock_arch_time_t)(((ms) * TIKU_CLOCK_ARCH_SECOND) / 1000))

#define TIKU_CLOCK_ARCH_TICKS_TO_MS(ticks) \
    ((unsigned long)(((ticks) * 1000) / TIKU_CLOCK_ARCH_SECOND))

#endif /* TIKU_STM32F411_TIMER_ARCH_H_ */
