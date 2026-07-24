/*
 * Tiku Operating System v0.06
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

/** @brief Convenience alias for the kernel's 16-bit hardware-timer clock
 *         type.  The htimer kernel header typedef'd tiku_htimer_clock_t as
 *         unsigned short (16-bit); reproducing it here avoids a long name
 *         throughout the driver. */
typedef tiku_htimer_clock_t htimer_t;

/** @brief ISR-fire counter, exposed for diagnostics.  The htimer test
 *         fleet was failing every "callback fired" assertion on this
 *         port; having a live counter the test can read tells us whether
 *         the ISR is even running, instead of guessing.  Also useful for
 *         any future "did the ISR storm during this op?" check. */
volatile uint32_t tiku_htimer_arch_isr_count;

/** @brief Initialise the RP2350 TIMER0 alarm-0 hardware for single-shot
 *         use.  Disarms any pre-existing alarm, masks the interrupt,
 *         clears any latched IRQ, and enables the NVIC line so that
 *         subsequent tiku_htimer_arch_schedule() calls can fire. */
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

/** @brief Read the current 16-bit hardware-timer value from TIMER0
 *         TIMERAWL (lower 32 bits of the 64-bit free-running counter).
 * @return Current 16-bit timer tick (wraps every ~65.5 ms at 1 MHz). */
htimer_t tiku_htimer_arch_now(void) {
    return (htimer_t)_RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

/** @brief Arm TIMER0 alarm-0 to fire at the 16-bit absolute tick @p t.
 *         The 16-bit target is sign-extended against the live 32-bit
 *         counter to form the full absolute compare value, then the IRQ
 *         is unmasked.  Any previously latched alarm IRQ is cleared first.
 * @param  t  Target 16-bit tick value (kernel htimer_clock_t domain). */
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

/** @brief TIMER0 alarm-0 IRQ handler (ISR context).  Increments the
 *         diagnostic fire counter, masks and clears the alarm interrupt,
 *         then dispatches the pending htimer callback via
 *         tiku_htimer_run_next(). */
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

/** @brief Diagnostic: read the raw 32-bit lower word of the TIMER0
 *         free-running counter (TIMERAWL).
 * @return Current TIMERAWL register value. */
uint32_t tiku_htimer_arch_diag_timerawl(void) {
    return _RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

/** @brief Diagnostic: read the TIMER0 raw interrupt status for alarm 0
 *         (INTR bit 0).
 * @return Non-zero if the alarm-0 raw interrupt is latched. */
uint32_t tiku_htimer_arch_diag_intr(void) {
    return _RP2350_REG(RP2350_TIMER0_INTR) & 0x1U;
}

/** @brief Diagnostic: read the TIMER0 interrupt-enable register for
 *         alarm 0 (INTE bit 0).
 * @return Non-zero if the alarm-0 interrupt is currently unmasked. */
uint32_t tiku_htimer_arch_diag_inte(void) {
    return _RP2350_REG(RP2350_TIMER0_INTE) & 0x1U;
}

/** @brief Diagnostic: check whether TIMER0_IRQ_0 is pending in the NVIC
 *         interrupt set-pending register (ISPR0).
 * @return Non-zero if the IRQ is pending in the NVIC. */
uint32_t tiku_htimer_arch_diag_nvic_pending(void) {
    return *(volatile uint32_t *)RP2350_NVIC_ISPR0 & (1U << RP2350_IRQ_TIMER0_0);
}

/** @brief Diagnostic: check whether TIMER0_IRQ_0 is enabled in the NVIC
 *         interrupt set-enable register (ISER0).
 * @return Non-zero if the IRQ line is enabled in the NVIC. */
uint32_t tiku_htimer_arch_diag_nvic_enabled(void) {
    return *(volatile uint32_t *)RP2350_NVIC_ISER0 & (1U << RP2350_IRQ_TIMER0_0);
}

/** @brief Diagnostic: read the TIMER0 masked interrupt status for alarm 0
 *         (INTS bit 0 — INTR AND INTE, after force).
 * @return Non-zero if the masked alarm-0 interrupt status is asserted. */
uint32_t tiku_htimer_arch_diag_ints(void) {
    return _RP2350_REG(RP2350_TIMER0_INTS) & 0x1U;
}

/** @brief Diagnostic: force-assert the TIMER0 alarm-0 IRQ via the INTF
 *         register, bypassing the INTR/INTE path.  If the ISR fires after
 *         this call the NVIC routing and vector table entry are correct;
 *         the fault lies upstream in the timer comparator chain. */
void tiku_htimer_arch_diag_force_irq(void) {
    /* INTF is "interrupt force" — writing 1 to a bit asserts the IRQ
     * regardless of INTR/INTE. If forcing this makes the ISR fire,
     * the NVIC routing and vector are correct and the issue is
     * upstream in the timer (comparator → INTR → INTE → INTS path). */
    _RP2350_REG_SET(RP2350_TIMER0_INTF, 0x1U);
}

/** @brief Diagnostic: de-assert the TIMER0 alarm-0 force bit in INTF,
 *         cancelling any IRQ that was asserted by
 *         tiku_htimer_arch_diag_force_irq(). */
void tiku_htimer_arch_diag_clear_force(void) {
    _RP2350_REG_CLR(RP2350_TIMER0_INTF, 0x1U);
}
