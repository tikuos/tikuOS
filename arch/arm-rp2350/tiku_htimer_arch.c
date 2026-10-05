/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - RP2350 hardware-timer driver (TIMER0 alarm 0).
 *
 * The kernel's htimer clock is 16 bits wide, so a target is extended to 32
 * bits against the current counter.  The alarm disarms itself when it fires
 * and the ISR masks it, so each schedule fires once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/** @brief Short local name for tiku_htimer_clock_t, the kernel's 16-bit
 *         (unsigned short) hardware-timer clock type. */
typedef tiku_htimer_clock_t htimer_t;

/** @brief TIMER0 alarm-0 interrupts taken since boot; the htimer tests read
 *         it to tell whether the ISR runs at all. */
volatile uint32_t tiku_htimer_arch_isr_count;

/** @brief Initialise the RP2350 TIMER0 alarm-0 hardware for single-shot
 *         use.  Disarms any pre-existing alarm, masks the interrupt,
 *         clears any latched IRQ, and enables the NVIC line so that
 *         subsequent tiku_htimer_arch_schedule() calls can fire. */
void tiku_htimer_arch_init(void) {
    /* Disarm any pre-existing ALARM0: ARMED bit 0 is write-1-to-disarm. */
    _RP2350_REG(RP2350_TIMER0_ARMED) = 0x1U;

    /* Mask the alarm 0 IRQ until something is scheduled. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);

    /* Clear any latched IRQ.  INTR is write-1-to-clear; a direct write
     * clears bit 0 only. */
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

    /* The Pico SDK's arming order: clear any latched IRQ, write ALARM0,
     * which arms the alarm, then unmask it in INTE.  A new ALARM0 value
     * replaces an armed one, so no disarm comes first. */
    _RP2350_REG(RP2350_TIMER0_INTR) = 0x1U;            /* W1C */
    _RP2350_REG(RP2350_TIMER0_ALARM0) = target;        /* arms */
    _RP2350_REG_SET(RP2350_TIMER0_INTE, 0x1U);         /* unmask */
}

/*---------------------------------------------------------------------------*/
/* IRQ HANDLER                                                               */
/*---------------------------------------------------------------------------*/

/** @brief TIMER0 alarm-0 IRQ handler (ISR context).  Increments the
 *         diagnostic fire counter, masks and clears the alarm interrupt,
 *         then dispatches the pending htimer callback via
 *         tiku_htimer_run_next(). */
void tiku_rp2350_timer0_alarm0_isr(void) {
    tiku_htimer_arch_isr_count++;

    /* Mask and acknowledge before the callback: a callback that does not
     * reschedule leaves the alarm masked.  INTR is write-1-to-clear; a
     * direct write clears bit 0 only. */
    _RP2350_REG_CLR(RP2350_TIMER0_INTE, 0x1U);
    _RP2350_REG(RP2350_TIMER0_INTR) = 0x1U;

    tiku_htimer_run_next();
}

/*---------------------------------------------------------------------------*/
/* DIAGNOSTICS                                                               */
/*---------------------------------------------------------------------------*/

/* Views of each stage between the counter and the ISR (counter, INTR, INTE,
 * INTS, NVIC) and a forced IRQ, for the htimer tests. */

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
 *         (INTS bit 0: INTR masked by INTE, or forced by INTF).
 * @return Non-zero if the masked alarm-0 interrupt status is asserted. */
uint32_t tiku_htimer_arch_diag_ints(void) {
    return _RP2350_REG(RP2350_TIMER0_INTS) & 0x1U;
}

/** @brief Diagnostic: force-assert the TIMER0 alarm-0 IRQ via the INTF
 *         register, bypassing the INTR/INTE path.  The ISR then runs if the
 *         NVIC line and the vector table entry work. */
void tiku_htimer_arch_diag_force_irq(void) {
    /* INTF bit 0 asserts the IRQ whatever INTR and INTE hold. */
    _RP2350_REG_SET(RP2350_TIMER0_INTF, 0x1U);
}

/** @brief Diagnostic: de-assert the TIMER0 alarm-0 force bit in INTF,
 *         cancelling any IRQ that was asserted by
 *         tiku_htimer_arch_diag_force_irq(). */
void tiku_htimer_arch_diag_clear_force(void) {
    _RP2350_REG_CLR(RP2350_TIMER0_INTF, 0x1U);
}
