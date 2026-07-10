/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - nRF54L hardware one-shot timer (TIMER20)
 *
 * The kernel htimer expresses deadlines as a 16-bit tick that wraps every
 * 65.536 ms at 1 MHz (tiku_htimer_clock_t == unsigned short).  We map that
 * onto TIMER20 run in 16-bit BITMODE at 1 MHz: the hardware counter *is* the
 * kernel's 16-bit clock, so a deadline maps straight onto a compare register
 * with no delta arithmetic.  now() captures the live count into CC[0];
 * schedule() arms CC[1] and unmasks its COMPARE interrupt.  The COMPARE1 ISR
 * masks itself (single-shot) and dispatches the pending callback through
 * tiku_htimer_run_next().  The counter free-runs the whole time so now()
 * always reflects real elapsed microseconds.
 *
 * TIMER20 sits in the main peripheral domain; its base clock is 16 MHz, so a
 * PRESCALER of 4 (divide-by-16) yields exactly 1 MHz.  TIMER00/TIMER10 are the
 * high-speed timers (used elsewhere / reserved for the tick fallback), so
 * TIMER20 is a conflict-free choice for the htimer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK register types + NRF_TIMER20_S */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers                    */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Config                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_HTIMER_TIMER       NRF_TIMER20_S
#define TIKU_HTIMER_IRQN        202            /* TIMER20_IRQn (MDK enum)        */
#define TIKU_HTIMER_PRESCALER   4UL            /* 16 MHz >> 4 = 1 MHz            */

#define TIKU_HTIMER_CC_NOW      0u             /* capture channel for now()      */
#define TIKU_HTIMER_CC_FIRE     1u             /* compare channel for the deadline */

/* INTENSET/INTENCLR bit for CC_FIRE (COMPARE1 lives at bit 17). */
#define TIKU_HTIMER_INTEN_FIRE  (1UL << (16 + TIKU_HTIMER_CC_FIRE))

/** @brief Convenience alias for the kernel's 16-bit hardware-timer clock. */
typedef tiku_htimer_clock_t htimer_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bring up TIMER20 as a free-running 1 MHz, 16-bit timebase.
 *
 * Stops the timer, clears all interrupt masks, selects Timer mode / 16-bit
 * width / divide-by-16 prescaler, zeroes the counter, then enables the NVIC
 * line and starts the counter.  Priority 1 keeps htimer callbacks ahead of
 * the console (2) and the kernel tick (3), matching the microsecond-class
 * intent of the htimer API.  No compare is armed until schedule() runs.
 */
void tiku_htimer_arch_init(void)
{
    TIKU_HTIMER_TIMER->TASKS_STOP  = 1UL;
    TIKU_HTIMER_TIMER->INTENCLR    = 0xFFFFFFFFUL;
    TIKU_HTIMER_TIMER->MODE        = 0UL;      /* TIMER_MODE_MODE_Timer          */
    TIKU_HTIMER_TIMER->BITMODE     = 0UL;      /* TIMER_BITMODE_BITMODE_16Bit    */
    TIKU_HTIMER_TIMER->PRESCALER   = TIKU_HTIMER_PRESCALER;
    TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] = 0UL;
    TIKU_HTIMER_TIMER->TASKS_CLEAR = 1UL;

    tiku_nordic_nvic_clear_pending(TIKU_HTIMER_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_HTIMER_IRQN, 1u);
    tiku_nordic_nvic_enable(TIKU_HTIMER_IRQN);

    TIKU_HTIMER_TIMER->TASKS_START = 1UL;      /* free-running from here          */
}

/**
 * @brief Read the current 16-bit hardware-timer tick.
 *
 * Triggers a CAPTURE of the live counter into CC[0] and returns the low
 * 16 bits (the timer is in 16-bit mode, so CC[0] already holds a 0..65535
 * value).  Wraps every ~65.5 ms at 1 MHz.
 *
 * @return Current 16-bit timer tick.
 */
htimer_t tiku_htimer_arch_now(void)
{
    TIKU_HTIMER_TIMER->TASKS_CAPTURE[TIKU_HTIMER_CC_NOW] = 1UL;
    return (htimer_t)TIKU_HTIMER_TIMER->CC[TIKU_HTIMER_CC_NOW];
}

/**
 * @brief Arm a single-shot compare to fire at the 16-bit absolute tick @p t.
 *
 * Because the counter runs in 16-bit mode, the kernel's absolute deadline
 * maps directly onto CC[1]: the COMPARE1 event fires once when the counter
 * next equals @p t (the kernel guarantees @p t is at least the htimer guard
 * time ahead, so the match has not already passed).  The stale event is
 * cleared before unmasking so a previous fire cannot re-trigger immediately.
 *
 * @param t  Target 16-bit tick value (kernel htimer_clock_t domain).
 */
void tiku_htimer_arch_schedule(htimer_t t)
{
    TIKU_HTIMER_TIMER->CC[TIKU_HTIMER_CC_FIRE] = (uint32_t)t;
    TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] = 0UL;
    (void)TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE];
    TIKU_HTIMER_TIMER->INTENSET = TIKU_HTIMER_INTEN_FIRE;
}

/*---------------------------------------------------------------------------*/
/* IRQ handler                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief TIMER20 COMPARE1 ISR: dispatch the expired htimer callback.
 *
 * Overrides the weak alias installed by the crt vector table (TIMER20_IRQn
 * 202).  Clears the compare event, masks the compare interrupt so a callback
 * that does not reschedule leaves the htimer idle, then runs the kernel's
 * pending-callback dispatcher in ISR context.
 */
void tiku_nordic_timer20_isr(void)
{
    if (TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] != 0UL) {
        TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] = 0UL;
        (void)TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE];
        TIKU_HTIMER_TIMER->INTENCLR = TIKU_HTIMER_INTEN_FIRE;   /* single-shot */

        tiku_htimer_run_next();
    }
}
