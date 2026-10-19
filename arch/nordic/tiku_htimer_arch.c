/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - nRF54L hardware one-shot timer (TIMER20).
 *
 * TIMER20 runs in 16-bit BITMODE at 1 MHz, so the counter is the kernel's
 * 16-bit clock and a deadline maps onto a compare register with no delta
 * arithmetic.  The COMPARE1 ISR masks itself, so each arm fires once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK types, NRF_TIMER20_S */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers             */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Config                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_HTIMER_TIMER       NRF_TIMER20_S
#define TIKU_HTIMER_IRQN        202            /* TIMER20_IRQn (MDK enum)  */
#define TIKU_HTIMER_PRESCALER   4UL            /* 16 MHz >> 4 = 1 MHz      */

#define TIKU_HTIMER_CC_NOW      0u             /* capture channel, now()   */
#define TIKU_HTIMER_CC_FIRE     1u             /* compare channel, deadline */

/* INTENSET/INTENCLR bit for CC_FIRE (COMPARE1 lives at bit 17). */
#define TIKU_HTIMER_INTEN_FIRE  (1UL << (16 + TIKU_HTIMER_CC_FIRE))

/** @brief Convenience alias for the kernel's 16-bit hardware-timer clock. */
typedef tiku_htimer_clock_t htimer_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_htimer_arch_init(void)
{
    /* TIMER20 free-runs as a 1 MHz, 16-bit timebase; no compare is armed
     * until tiku_htimer_arch_schedule() runs. */
    TIKU_HTIMER_TIMER->TASKS_STOP  = 1UL;
    TIKU_HTIMER_TIMER->INTENCLR    = 0xFFFFFFFFUL;
    TIKU_HTIMER_TIMER->MODE        = 0UL;      /* TIMER_MODE_MODE_Timer       */
    TIKU_HTIMER_TIMER->BITMODE     = 0UL;      /* TIMER_BITMODE_BITMODE_16Bit */
    TIKU_HTIMER_TIMER->PRESCALER   = TIKU_HTIMER_PRESCALER;
    TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] = 0UL;
    TIKU_HTIMER_TIMER->TASKS_CLEAR = 1UL;

    tiku_nordic_nvic_clear_pending(TIKU_HTIMER_IRQN);
    /* Priority 1 runs htimer callbacks ahead of the console (2) and the
     * kernel tick (3). */
    tiku_nordic_nvic_set_priority(TIKU_HTIMER_IRQN, 1u);
    tiku_nordic_nvic_enable(TIKU_HTIMER_IRQN);

    TIKU_HTIMER_TIMER->TASKS_START = 1UL;      /* free-running from here      */
}

htimer_t tiku_htimer_arch_now(void)
{
    /* The capture into CC[0] holds 0..65535 in 16-bit mode; the count wraps
     * every 65.536 ms at 1 MHz. */
    TIKU_HTIMER_TIMER->TASKS_CAPTURE[TIKU_HTIMER_CC_NOW] = 1UL;
    return (htimer_t)TIKU_HTIMER_TIMER->CC[TIKU_HTIMER_CC_NOW];
}

void tiku_htimer_arch_schedule(htimer_t t)
{
    /* The absolute 16-bit deadline goes straight into CC[1]: COMPARE1 fires
     * once, when the counter next equals t, and its ISR masks it again.  The
     * stale event is cleared before unmasking, so a previous fire does not
     * re-trigger. */
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
 * Overrides the weak alias installed by the crt vector table.  Clears the
 * compare event, masks the compare interrupt so a callback that does not
 * reschedule leaves the htimer idle, then runs the pending-callback dispatcher.
 */
void tiku_nordic_timer20_isr(void)
{
    if (TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] != 0UL) {
        TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE] = 0UL;
        (void)TIKU_HTIMER_TIMER->EVENTS_COMPARE[TIKU_HTIMER_CC_FIRE];
        TIKU_HTIMER_TIMER->INTENCLR = TIKU_HTIMER_INTEN_FIRE; /* single-shot */

        tiku_htimer_run_next();
    }
}
