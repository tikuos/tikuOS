/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - nRF54L system tick @ 128 Hz
 *
 * Default source is the GRTC (Global RTC): its 1 MHz SYSCOUNTER already runs
 * (boot ROM starts it) in the always-on low-frequency domain, so the tick
 * survives deep sleep -- the low-power foundation.  A free compare channel
 * (CC0) is armed each tick with an alternating 7812/7813-count step (average
 * 7812.5 = exact 128 Hz, no drift), re-armed RELATIVE TO THE PREVIOUS COMPARE
 * (CCADD reference = CC) so interrupt latency never accumulates.  The COMPARE0
 * interrupt advances the software tick and wakes the timer-poll process.
 *
 * Build with -DTIKU_NORDIC_TICK_TIMER10 to use the bring-up tick instead: a
 * 32-bit TIMER10 at 16 MHz, CC0=125000, COMPARE0->CLEAR short.  It is exactly
 * 128 Hz too, but runs off PCLK16M and stops once HFCLK gates in deep sleep.
 *
 * SysTick is left free for busy-delays (see tiku_cpu_common.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_timer_arch.h"
#include <arch/nordic/mdk/nrf54l15.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <kernel/scheduler/tiku_sched.h>
#include <stdint.h>

/* Tick source selection: GRTC by default, TIMER10 on request. */
#if defined(TIKU_NORDIC_TICK_TIMER10)
#define TIKU_NORDIC_TICK_GRTC   0
#else
#define TIKU_NORDIC_TICK_GRTC   1
#endif

/** @brief Tick counter, incremented by the tick ISR. */
static volatile tiku_clock_arch_time_t g_ticks   = 0UL;
/** @brief Seconds counter, derived from g_ticks at TIKU_CLOCK_ARCH_SECOND. */
static volatile unsigned long          g_seconds = 0UL;

#if TIKU_NORDIC_TICK_GRTC
/*===========================================================================*/
/* GRTC tick (default)                                                       */
/*===========================================================================*/

#define TIKU_GRTC             NRF_GRTC_S
#define TIKU_GRTC_IRQN        226            /* GRTC_0_IRQn (MDK enum)        */
#define TIKU_GRTC_TICK_CC     0              /* compare channel for the tick  */
#define TIKU_GRTC_CAP_CC      1             /* channel used to capture SYSCNT */
#define TIKU_GRTC_HZ          1000000UL      /* SYSCOUNTER runs at 1 MHz      */
/* 1 MHz / 128 = 7812.5 counts/tick -> alternate 7812/7813 for an exact mean. */
#define TIKU_GRTC_STEP_LO     (TIKU_GRTC_HZ / TIKU_CLOCK_ARCH_SECOND)     /* 7812 */
#define TIKU_GRTC_STEP_NOM    (TIKU_GRTC_STEP_LO + 1UL)                   /* 7813 */
#define TIKU_GRTC_CCADD_REF_SYS  0UL            /* CCADD reference = SYSCOUNTER */
#define TIKU_GRTC_CCADD_REF_CC   (1UL << 31)    /* CCADD reference = CC[n]      */
#define TIKU_GRTC_CCEN_ENABLE    1UL
#define TIKU_GRTC_MODE_SYSCNTEN  (1UL << 1)     /* MODE.SYSCOUNTEREN            */

void tiku_clock_arch_init(void)
{
    g_ticks   = 0UL;
    g_seconds = 0UL;

    /* The boot ROM normally leaves the 1 MHz SYSCOUNTER running; start it if a
     * given boot path did not (idempotent -- never touched while running, so
     * the free-running count is not disturbed). */
    if ((TIKU_GRTC->MODE & TIKU_GRTC_MODE_SYSCNTEN) == 0UL) {
        TIKU_GRTC->MODE |= TIKU_GRTC_MODE_SYSCNTEN;
        TIKU_GRTC->TASKS_START = 1UL;
    }

    /* Disarm the tick channel + clear any stale state before arming. */
    TIKU_GRTC->INTENCLR0 = (1UL << TIKU_GRTC_TICK_CC);
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = 0UL;
    TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] = 0UL;

    /* Arm CC0 = SYSCOUNTER(now) + one nominal step, then enable the compare. */
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCADD =
        TIKU_GRTC_CCADD_REF_SYS | TIKU_GRTC_STEP_NOM;
    TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = TIKU_GRTC_CCEN_ENABLE;

    /* Enable the COMPARE0 interrupt.  IRQs are still globally masked (the reset
     * handler ran cpsid i); the scheduler unmasks them at the top of its loop,
     * so the first tick cannot fire before the process queues are built. */
    TIKU_GRTC->INTENSET0 = (1UL << TIKU_GRTC_TICK_CC);

    tiku_nordic_nvic_clear_pending(TIKU_GRTC_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_GRTC_IRQN, 3u);
    tiku_nordic_nvic_enable(TIKU_GRTC_IRQN);
}

/**
 * @brief GRTC COMPARE0 ISR: advance the tick, re-arm, wake the timer poll.
 *
 * Overrides the weak alias installed by the crt vector table (IRQn 226).
 */
void tiku_nordic_grtc_isr(void)
{
    if (TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] != 0UL) {
        TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC] = 0UL;
        (void)TIKU_GRTC->EVENTS_COMPARE[TIKU_GRTC_TICK_CC];   /* flush clear */

        g_ticks++;
        if ((g_ticks % (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND) == 0UL) {
            g_seconds++;
        }

        /* Advance the compare by the next step, RELATIVE TO THE PREVIOUS
         * compare (reference = CC) so ISR latency never accumulates; alternate
         * 7812/7813 so the long-run mean is exactly 7812.5 = 128 Hz. */
        TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCADD =
            TIKU_GRTC_CCADD_REF_CC |
            (TIKU_GRTC_STEP_LO + (uint32_t)(g_ticks & 1UL));
        TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCEN = TIKU_GRTC_CCEN_ENABLE;

        tiku_sched_notify();
    }
}

unsigned short tiku_clock_arch_fine(void)
{
    uint32_t now, target, remaining, pos;

    /* Snapshot the live SYSCOUNTER (low word) into a spare capture channel,
     * then measure how far we are into the current tick: the tick channel's
     * CC holds the NEXT tick's counter target, so (target - now) counts remain.
     * Unsigned subtraction is wrap-safe across the 32-bit low-word boundary. */
    TIKU_GRTC->TASKS_CAPTURE[TIKU_GRTC_CAP_CC] = 1UL;
    now       = TIKU_GRTC->CC[TIKU_GRTC_CAP_CC].CCL;
    target    = TIKU_GRTC->CC[TIKU_GRTC_TICK_CC].CCL;
    remaining = target - now;
    if (remaining > TIKU_GRTC_STEP_NOM) {
        remaining = TIKU_GRTC_STEP_NOM;      /* just fired / not yet re-armed */
    }
    pos = TIKU_GRTC_STEP_NOM - remaining;     /* counts elapsed this tick      */
    return (unsigned short)((pos * 0xFFFFUL) / TIKU_GRTC_STEP_NOM);
}

#else /* !TIKU_NORDIC_TICK_GRTC */
/*===========================================================================*/
/* TIMER10 tick (fallback: -DTIKU_NORDIC_TICK_TIMER10)                       */
/*===========================================================================*/

#define TIKU_TIMER10             NRF_TIMER10_S
#define TIKU_TIMER10_IRQN        133   /* TIMER10_IRQn (MDK enum) */
#define TIKU_TIMER_HZ            16000000UL
#define TIKU_TIMER_INTERVAL      (TIKU_TIMER_HZ / TIKU_CLOCK_ARCH_SECOND)  /* 125000 */
#define TIMER_MODE_TIMER         0UL
#define TIMER_BITMODE_32BIT      3UL
#define TIMER_PRESCALER_16MHZ    0UL
#define TIMER_SHORTS_C0_CLEAR    (1UL << 0)
#define TIMER_INTENSET_COMPARE0  (1UL << 16)

void tiku_clock_arch_init(void)
{
    g_ticks   = 0UL;
    g_seconds = 0UL;

    TIKU_TIMER10->TASKS_STOP  = 1UL;
    TIKU_TIMER10->TASKS_CLEAR = 1UL;

    TIKU_TIMER10->MODE      = TIMER_MODE_TIMER;
    TIKU_TIMER10->BITMODE   = TIMER_BITMODE_32BIT;
    TIKU_TIMER10->PRESCALER = TIMER_PRESCALER_16MHZ;      /* 16 MHz          */
    TIKU_TIMER10->CC[0]     = TIKU_TIMER_INTERVAL;        /* 125000 => 128Hz */
    TIKU_TIMER10->SHORTS    = TIMER_SHORTS_C0_CLEAR;      /* auto-wrap       */

    TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
    TIKU_TIMER10->INTENSET = TIMER_INTENSET_COMPARE0;

    tiku_nordic_nvic_clear_pending(TIKU_TIMER10_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_TIMER10_IRQN, 3u);
    tiku_nordic_nvic_enable(TIKU_TIMER10_IRQN);

    TIKU_TIMER10->TASKS_START = 1UL;
}

/** @brief TIMER10 compare ISR (crt vector IRQn 133). */
void tiku_nordic_timer10_isr(void)
{
    if (TIKU_TIMER10->EVENTS_COMPARE[0] != 0UL) {
        TIKU_TIMER10->EVENTS_COMPARE[0] = 0UL;
        (void)TIKU_TIMER10->EVENTS_COMPARE[0];   /* read-back: flush clear */

        g_ticks++;
        if ((g_ticks % (tiku_clock_arch_time_t)TIKU_CLOCK_ARCH_SECOND) == 0UL) {
            g_seconds++;
        }
        tiku_sched_notify();
    }
}

unsigned short tiku_clock_arch_fine(void)
{
    uint32_t count;

    TIKU_TIMER10->TASKS_CAPTURE[1] = 1UL;
    count = TIKU_TIMER10->CC[1];
    if (count >= TIKU_TIMER_INTERVAL) {
        return 0xFFFFu;
    }
    return (unsigned short)((count * 0xFFFFUL) / TIKU_TIMER_INTERVAL);
}

#endif /* TIKU_NORDIC_TICK_GRTC */

/*===========================================================================*/
/* Shared queries (source-independent)                                       */
/*===========================================================================*/

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

int tiku_clock_arch_fine_max(void)
{
    return 0xFFFF;
}
