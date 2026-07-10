/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - nRF54L system tick (TIMER10 compare @ 128 Hz)
 *
 * TIMER10 runs in 32-bit Timer mode at 16 MHz (PRESCALER 0).  CC0 = 125000
 * with a COMPARE0->CLEAR short makes the timer wrap every 1/128 s exactly;
 * the COMPARE0 interrupt increments the software tick and wakes the timer-poll
 * process so software timers expire.  SysTick is left free for busy-delays
 * (see tiku_cpu_common.c).  A low-power GRTC tick is a planned follow-up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include <arch/nordic/mdk/nrf54l15.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <kernel/scheduler/tiku_sched.h>
#include <stdint.h>

#define TIKU_TIMER10             NRF_TIMER10_S
#define TIKU_TIMER10_IRQN        133   /* TIMER10_IRQn (MDK enum) */
#define TIMER_MODE_TIMER         0UL
#define TIMER_BITMODE_32BIT      3UL
#define TIMER_PRESCALER_16MHZ    0UL
#define TIMER_SHORTS_C0_CLEAR    (1UL << 0)
#define TIMER_INTENSET_COMPARE0  (1UL << 16)

/** @brief Tick counter, incremented by the TIMER10 compare ISR. */
static volatile tiku_clock_arch_time_t g_ticks   = 0UL;
/** @brief Seconds counter, derived from g_ticks at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned long          g_seconds = 0UL;

/*---------------------------------------------------------------------------*/
/* Init                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_clock_arch_init(void)
{
    /* Reset the software accumulators (unlike the MSP430 Timer A0 which zeroes
     * for free) so re-init leaves a clean elapsed time. */
    g_ticks   = 0UL;
    g_seconds = 0UL;

    TIKU_TIMER10->TASKS_STOP  = 1UL;
    TIKU_TIMER10->TASKS_CLEAR = 1UL;

    TIKU_TIMER10->MODE      = TIMER_MODE_TIMER;
    TIKU_TIMER10->BITMODE   = TIMER_BITMODE_32BIT;
    TIKU_TIMER10->PRESCALER = TIMER_PRESCALER_16MHZ;      /* 16 MHz         */
    TIKU_TIMER10->CC[0]     = TIKU_CLOCK_ARCH_INTERVAL;   /* 125000 => 128Hz*/
    TIKU_TIMER10->SHORTS    = TIMER_SHORTS_C0_CLEAR;      /* auto-wrap      */

    TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
    TIKU_TIMER10->INTENSET = TIMER_INTENSET_COMPARE0;

    /* Enable the TIMER10 IRQ at the NVIC.  IRQs are still globally masked
     * (the reset handler ran cpsid i); the scheduler unmasks them at the top
     * of its loop, so the first tick cannot fire before the queues are up. */
    tiku_nordic_nvic_clear_pending(TIKU_TIMER10_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_TIMER10_IRQN, 3u);
    tiku_nordic_nvic_enable(TIKU_TIMER10_IRQN);

    TIKU_TIMER10->TASKS_START = 1UL;
}

/*---------------------------------------------------------------------------*/
/* ISR                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief TIMER10 compare ISR: advance the tick, wake the timer-poll process.
 *
 * Overrides the weak alias installed by the crt vector table.
 */
void tiku_nordic_timer10_isr(void)
{
    if (TIKU_TIMER10->EVENTS_COMPARE[0] != 0UL) {
        TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
        (void)TIKU_TIMER10->EVENTS_COMPARE[0];   /* read-back: flush clear  */

        g_ticks++;
        if ((g_ticks % (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND) == 0UL) {
            g_seconds++;
        }
        tiku_sched_notify();
    }
}

/*---------------------------------------------------------------------------*/
/* Queries                                                                   */
/*---------------------------------------------------------------------------*/

tiku_clock_arch_time_t tiku_clock_arch_time(void)
{
    return g_ticks;
}

unsigned long tiku_clock_arch_seconds(void)
{
    return g_seconds;
}

void tiku_clock_arch_set_seconds(unsigned long sec)
{
    g_seconds = sec;
}

void tiku_clock_arch_wait(tiku_clock_arch_time_t t)
{
    /* Wraparound-safe: spin while t is still in the future. */
    while ((long)(t - g_ticks) > 0) {
        /* busy-wait for the tick ISR to advance g_ticks */
    }
}

void tiku_clock_arch_delay(unsigned int us)
{
    tiku_cpu_nordic_delay_us((uint32_t)us);
}

unsigned short tiku_clock_arch_fine(void)
{
    uint32_t count;

    /* Capture the live TIMER10 value into CC[1] and scale the position within
     * the current tick to a 16-bit fraction. */
    TIKU_TIMER10->TASKS_CAPTURE[1] = 1UL;
    count = TIKU_TIMER10->CC[1];
    if (count >= TIKU_CLOCK_ARCH_INTERVAL) {
        return 0xFFFFu;
    }
    return (unsigned short)((count * 0xFFFFUL) / TIKU_CLOCK_ARCH_INTERVAL);
}

int tiku_clock_arch_fine_max(void)
{
    return 0xFFFF;
}
