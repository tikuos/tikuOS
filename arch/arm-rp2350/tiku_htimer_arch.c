/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - RP2350 hardware-timer driver (TIMER0 alarm 0)
 *
 * The kernel htimer uses a 16-bit clock that wraps every 65.5 ms
 * (at 1 MHz). We compose a 32-bit absolute target by extending the
 * 16-bit delta against the current timer reading. ALARM0 fires when
 * TIMELR matches; clearing the ARMED bit in the ISR (and then
 * re-arming on the next schedule call) gives us the single-shot
 * semantics the kernel expects.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/* The htimer kernel header typedef'd tiku_htimer_clock_t as
 * unsigned short (16-bit). Reproduce that here for clarity. */
typedef tiku_htimer_clock_t htimer_t;

/* ISR-fire counter, exposed for diagnostics. The htimer test fleet
 * was failing every "callback fired" assertion on this port; having
 * a live counter the test can read tells us whether the ISR is even
 * running, instead of guessing. Also useful for any future "did the
 * ISR storm during this op?" check. */
volatile uint32_t tiku_htimer_arch_isr_count;

void tiku_htimer_arch_init(void) {
    /* Disarm any pre-existing ALARM0 by writing to ARMED bit 0
     * (write-1-to-disarm, per RP2350 datasheet 12.7.2). */
    _RP2350_REG(RP2350_TIMER0_ARMED) = 0x1U;

    /* Mask the alarm 0 IRQ until something is scheduled. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);

    /* Clear any latched IRQ. INTR is W1C — direct write, not the
     * SET alias (SET would do an unnecessary read-modify-write). */
    _RP2350_REG(RP2350_TIMER0_INTR) = 0x1U;

    /* Enable TIMER0_IRQ_0 in the NVIC. */
    rp2350_nvic_clear_pending(RP2350_IRQ_TIMER0_0);
    rp2350_nvic_enable(RP2350_IRQ_TIMER0_0);
}

htimer_t tiku_htimer_arch_now(void) {
    return (htimer_t)_RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

void tiku_htimer_arch_schedule(htimer_t t) {
    /* Compose absolute 32-bit target from the kernel's 16-bit time
     * by adding the signed 16-bit delta to the current 32-bit reading. */
    uint32_t now32   = _RP2350_REG(RP2350_TIMER0_TIMERAWL);
    int16_t  delta16 = (int16_t)((uint16_t)t - (uint16_t)now32);
    uint32_t target  = now32 + (int32_t)delta16;

    /* Match the Pico SDK arming sequence: clear any latched IRQ, write
     * ALARM0 (which arms it), enable INTE. The previous version also
     * wrote ARMED=0x1 to pre-disarm; that's redundant (ALARM0 write is
     * the one that arms) and could race with the subsequent ALARM0
     * write on tight reschedule paths. */
    _RP2350_REG(RP2350_TIMER0_INTR) = 0x1U;            /* W1C */
    _RP2350_REG(RP2350_TIMER0_ALARM0) = target;        /* arms */
    _RP2350_REG_SET(RP2350_TIMER0_INTE, 0x1U);         /* unmask */
}

/*---------------------------------------------------------------------------*/
/* IRQ handler                                                               */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_timer0_alarm0_isr(void) {
    tiku_htimer_arch_isr_count++;

    /* Mask the IRQ first so a callback that doesn't reschedule won't
     * leave the alarm re-firing immediately on its way back. INTR is
     * W1C — direct write, not the SET alias. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);
    _RP2350_REG(RP2350_TIMER0_INTR) = 0x1U;

    tiku_htimer_run_next();
}

/*---------------------------------------------------------------------------*/
/* Diagnostics                                                               */
/*                                                                            */
/* Used by the htimer test fleet to localise "ISR doesn't fire" failures    */
/* to a specific link in the chain (counter / comparator / NVIC / vector).  */
/*---------------------------------------------------------------------------*/

uint32_t tiku_htimer_arch_diag_timerawl(void) {
    return _RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

uint32_t tiku_htimer_arch_diag_intr(void) {
    return _RP2350_REG(RP2350_TIMER0_INTR) & 0x1U;
}

uint32_t tiku_htimer_arch_diag_inte(void) {
    return _RP2350_REG(RP2350_TIMER0_INTE) & 0x1U;
}

uint32_t tiku_htimer_arch_diag_nvic_pending(void) {
    return *(volatile uint32_t *)RP2350_NVIC_ISPR0 & (1U << RP2350_IRQ_TIMER0_0);
}

uint32_t tiku_htimer_arch_diag_nvic_enabled(void) {
    return *(volatile uint32_t *)RP2350_NVIC_ISER0 & (1U << RP2350_IRQ_TIMER0_0);
}

uint32_t tiku_htimer_arch_diag_ints(void) {
    return _RP2350_REG(RP2350_TIMER0_INTS) & 0x1U;
}

void tiku_htimer_arch_diag_force_irq(void) {
    /* INTF is "interrupt force" — writing 1 to a bit asserts the IRQ
     * regardless of INTR/INTE. If forcing this makes the ISR fire,
     * the NVIC routing and vector are correct and the issue is
     * upstream in the timer (comparator → INTR → INTE → INTS path). */
    _RP2350_REG_SET(RP2350_TIMER0_INTF, 0x1U);
}

void tiku_htimer_arch_diag_clear_force(void) {
    _RP2350_REG_CLR(RP2350_TIMER0_INTF, 0x1U);
}
