/*
 * Tiku Operating System v0.04
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

void tiku_htimer_arch_init(void) {
    /* Disarm any pre-existing ALARM0 by writing to the ARMED set
     * register (bit 0 = ALARM0). */
    _RP2350_REG(RP2350_TIMER0_ARMED) = 0x1U;

    /* Mask the alarm 0 IRQ until something is scheduled. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);

    /* Clear any latched IRQ. */
    _RP2350_REG_SET(RP2350_TIMER0_INTR, 0x1U);

    /* Enable TIMER0_IRQ_0 in the NVIC. */
    rp2350_nvic_clear_pending(RP2350_IRQ_TIMER0_0);
    rp2350_nvic_enable(RP2350_IRQ_TIMER0_0);
}

htimer_t tiku_htimer_arch_now(void) {
    return (htimer_t)_RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

void tiku_htimer_arch_schedule(htimer_t t) {
    /* Full 32-bit "now"; compose absolute target by adding the signed
     * 16-bit delta. If the kernel ever asks for a target in the past
     * (negative delta) the alarm will fire immediately. */
    uint32_t now32   = _RP2350_REG(RP2350_TIMER0_TIMERAWL);
    int16_t  delta16 = (int16_t)((uint16_t)t - (uint16_t)now32);
    uint32_t target  = now32 + (int32_t)delta16;

    /* Disarm before reprogramming so a stale match doesn't fire. */
    _RP2350_REG(RP2350_TIMER0_ARMED) = 0x1U;
    _RP2350_REG_SET(RP2350_TIMER0_INTR, 0x1U);

    /* Writing ALARM0 arms it. */
    _RP2350_REG(RP2350_TIMER0_ALARM0) = target;

    /* Unmask the IRQ. */
    _RP2350_REG_SET(RP2350_TIMER0_INTE, 0x1U);
}

/*---------------------------------------------------------------------------*/
/* IRQ handler                                                               */
/*---------------------------------------------------------------------------*/

void tiku_rp2350_timer0_alarm0_isr(void) {
    /* Mask the IRQ first so a callback that doesn't reschedule won't
     * leave the alarm re-firing immediately on its way back. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);
    _RP2350_REG_SET(RP2350_TIMER0_INTR, 0x1U);

    tiku_htimer_run_next();
}
