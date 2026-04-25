/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit.c - Critical execution window implementation
 *
 * Two flavours: defer-only (no IE masking) and masked
 * (selective IE masking via preserve_mask). End restores whatever
 * begin set up; the mode is recorded so end can short-circuit the
 * unmask path when nothing was masked in the first place.
 *
 * IE masking is MSP430-specific. Each subsystem family is guarded
 * by the corresponding register's compile-time visibility so this
 * file compiles unchanged across FR5969, FR5994, FR2433, etc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_crit.h"
#include "tiku_htimer.h"
#include "tiku_timer.h"
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* MODES                                                                     */
/*---------------------------------------------------------------------------*/

#define CRIT_MODE_NONE   0  /* Window not held */
#define CRIT_MODE_DEFER  1  /* Defer-only; no IE bits touched */
#define CRIT_MODE_MASKED 2  /* IE bits masked per preserve_mask */

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

volatile uint8_t tiku_crit_held;

static tiku_htimer_clock_t crit_start_htime;
static uint16_t            crit_max_us;
static uint16_t            crit_violations;
static uint16_t            crit_enters;
static uint8_t             crit_mode;

/*
 * IE state captured at begin (masked flavour only). Each field
 * stores the cleared register's prior value so end can restore
 * exactly what the caller had configured. Conditionally compiled
 * per device so a missing peripheral does not leave dead BSS.
 */
static struct {
    uint16_t ta0_ccie;
    uint16_t ta1_ccie;
#if defined(UCA0IE)
    uint16_t uca0_ie;
#endif
#if defined(UCA1IE)
    uint16_t uca1_ie;
#endif
#if defined(UCB0IE)
    uint16_t ucb0_ie;
#endif
#if defined(UCB1IE)
    uint16_t ucb1_ie;
#endif
#if defined(ADC12IER0)
    uint16_t adc12_ier0;
#endif
#if defined(SFRIE1)
    uint8_t  sfrie1_wdtie;
#endif
#if defined(P1IE)
    uint8_t  p1_ie;
#endif
#if defined(P2IE)
    uint8_t  p2_ie;
#endif
#if defined(P3IE)
    uint8_t  p3_ie;
#endif
#if defined(P4IE)
    uint8_t  p4_ie;
#endif
} crit_ie_saved;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * Convert microseconds to htimer ticks at compile-time-known rate.
 *
 * Two-step division avoids 32-bit overflow at 1 MHz htimer × 65 535 us
 * (which would be 6.5e10, larger than 2^32). Requires htimer >= 1 kHz,
 * which is true for every preset in tiku_htimer_config.h.
 */
static inline tiku_htimer_clock_t
crit_us_to_ticks(uint16_t us)
{
#if (TIKU_HTIMER_SECOND >= 1000UL)
    return (tiku_htimer_clock_t)
           (((uint32_t)us * (TIKU_HTIMER_SECOND / 1000UL)) / 1000UL);
#else
    return (tiku_htimer_clock_t)
           (((uint32_t)us * TIKU_HTIMER_SECOND) / 1000000UL);
#endif
}

/*---------------------------------------------------------------------------*/
/* INTERRUPT MASKING (MSP430)                                                */
/*---------------------------------------------------------------------------*/

/**
 * Snapshot and clear every peripheral IE bit family not in
 * preserve_mask.
 *
 * GIE remains enabled so any preserved ISR (typically the bit
 * clock) keeps firing. Each register is touched at most once.
 *
 * Devices vary in which peripherals exist; the #if defined()
 * guards skip families that the current MCU lacks.
 */
static void
crit_arch_mask_irqs(uint8_t preserve_mask)
{
    /* Timers A0 / A1 (always present on MSP430) */
    crit_ie_saved.ta0_ccie = TA0CCTL0 & CCIE;
    crit_ie_saved.ta1_ccie = TA1CCTL0 & CCIE;
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_TICK)) {
        TA0CCTL0 &= ~CCIE;
    }
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_HTIMER)) {
        TA1CCTL0 &= ~CCIE;
    }

#if defined(UCA0IE) || defined(UCA1IE)
    /* eUSCI_A: UART (and SPI when configured as such) */
#if defined(UCA0IE)
    crit_ie_saved.uca0_ie = UCA0IE;
#endif
#if defined(UCA1IE)
    crit_ie_saved.uca1_ie = UCA1IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_UART)) {
#if defined(UCA0IE)
        UCA0IE = 0;
#endif
#if defined(UCA1IE)
        UCA1IE = 0;
#endif
    }
#endif

#if defined(UCB0IE) || defined(UCB1IE)
    /* eUSCI_B: I2C and SPI */
#if defined(UCB0IE)
    crit_ie_saved.ucb0_ie = UCB0IE;
#endif
#if defined(UCB1IE)
    crit_ie_saved.ucb1_ie = UCB1IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_I2C)) {
#if defined(UCB0IE)
        UCB0IE = 0;
#endif
#if defined(UCB1IE)
        UCB1IE = 0;
#endif
    }
#endif

#if defined(ADC12IER0)
    /* ADC12 done */
    crit_ie_saved.adc12_ier0 = ADC12IER0;
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_ADC)) {
        ADC12IER0 = 0;
    }
#endif

#if defined(SFRIE1) && defined(WDTIE)
    /* Watchdog interval-mode IRQ. The watchdog *reset* is
     * independent and hardware -- not maskable from here. */
    crit_ie_saved.sfrie1_wdtie = SFRIE1 & WDTIE;
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_WDT)) {
        SFRIE1 &= ~WDTIE;
    }
#endif

    /* External GPIO edge ISRs */
#if defined(P1IE)
    crit_ie_saved.p1_ie = P1IE;
#endif
#if defined(P2IE)
    crit_ie_saved.p2_ie = P2IE;
#endif
#if defined(P3IE)
    crit_ie_saved.p3_ie = P3IE;
#endif
#if defined(P4IE)
    crit_ie_saved.p4_ie = P4IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_GPIO)) {
#if defined(P1IE)
        P1IE = 0;
#endif
#if defined(P2IE)
        P2IE = 0;
#endif
#if defined(P3IE)
        P3IE = 0;
#endif
#if defined(P4IE)
        P4IE = 0;
#endif
    }
}

/**
 * Restore the IE bits captured at begin.
 *
 * Lost-interrupt notes:
 *   - Timer A0: while masked, CCIFG can latch only one missed
 *     tick. Multiple missed compares collapse to a single
 *     post-window ISR. tiku_arch_count slips by the residual.
 *   - UART RX: bytes received during the window are lost beyond
 *     the 1-byte hardware buffer. UCAxIFG is preserved across
 *     IE clear/set, so the most recent byte may still trigger
 *     an ISR after restore.
 *   - GPIO edges: PxIFG latches a single edge per pin; multiple
 *     edges across the window collapse to one.
 *   - ADC done / I2C / WDT: completion flags are preserved in
 *     hardware; one ISR fires after restore for whatever was
 *     pending.
 */
static void
crit_arch_unmask_irqs(void)
{
    TA0CCTL0 = (TA0CCTL0 & ~CCIE) | crit_ie_saved.ta0_ccie;
    TA1CCTL0 = (TA1CCTL0 & ~CCIE) | crit_ie_saved.ta1_ccie;

#if defined(UCA0IE)
    UCA0IE = crit_ie_saved.uca0_ie;
#endif
#if defined(UCA1IE)
    UCA1IE = crit_ie_saved.uca1_ie;
#endif

#if defined(UCB0IE)
    UCB0IE = crit_ie_saved.ucb0_ie;
#endif
#if defined(UCB1IE)
    UCB1IE = crit_ie_saved.ucb1_ie;
#endif

#if defined(ADC12IER0)
    ADC12IER0 = crit_ie_saved.adc12_ier0;
#endif

#if defined(SFRIE1) && defined(WDTIE)
    SFRIE1 = (SFRIE1 & ~WDTIE) | crit_ie_saved.sfrie1_wdtie;
#endif

#if defined(P1IE)
    P1IE = crit_ie_saved.p1_ie;
#endif
#if defined(P2IE)
    P2IE = crit_ie_saved.p2_ie;
#endif
#if defined(P3IE)
    P3IE = crit_ie_saved.p3_ie;
#endif
#if defined(P4IE)
    P4IE = crit_ie_saved.p4_ie;
#endif
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_crit_begin(uint16_t max_us, uint8_t preserve_mask)
{
    if (tiku_crit_held) {
        return TIKU_CRIT_ERR_BUSY;
    }

    crit_start_htime = TIKU_HTIMER_NOW();
    crit_max_us      = max_us;
    crit_enters++;

    /*
     * Mask first, then set the held flag. The reverse order would
     * leave a few cycles where tiku_crit_active() returns true
     * but the not-yet-masked ISRs can still fire and inject jitter.
     */
    crit_arch_mask_irqs(preserve_mask);
    crit_mode      = CRIT_MODE_MASKED;
    tiku_crit_held = 1;
    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_crit_begin_defer(uint16_t max_us)
{
    if (tiku_crit_held) {
        return TIKU_CRIT_ERR_BUSY;
    }

    crit_start_htime = TIKU_HTIMER_NOW();
    crit_max_us      = max_us;
    crit_enters++;

    /* No IE-bit changes; just flip the dispatcher-defer flag. */
    crit_mode      = CRIT_MODE_DEFER;
    tiku_crit_held = 1;
    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

int tiku_crit_end(void)
{
    tiku_htimer_clock_t elapsed_ticks;
    uint8_t was_mode;

    if (!tiku_crit_held) {
        return TIKU_CRIT_ERR_NOT_HELD;
    }

    elapsed_ticks = (tiku_htimer_clock_t)
                    (TIKU_HTIMER_NOW() - crit_start_htime);

    /* Snapshot and clear mode + held flag before restoring IRQs
     * so any tick that fires immediately on unmask follows the
     * normal poll path. */
    was_mode       = crit_mode;
    crit_mode      = CRIT_MODE_NONE;
    tiku_crit_held = 0;

    if (was_mode == CRIT_MODE_MASKED) {
        crit_arch_unmask_irqs();
    }

    if (crit_max_us != 0) {
        tiku_htimer_clock_t budget_ticks = crit_us_to_ticks(crit_max_us);
        if (elapsed_ticks > budget_ticks) {
            crit_violations++;
        }
    }

    /* Drain any timer expirations that came due during the window. */
    tiku_timer_request_poll();

    return TIKU_CRIT_OK;
}

/*---------------------------------------------------------------------------*/

uint16_t tiku_crit_violation_count(void)
{
    return crit_violations;
}

uint16_t tiku_crit_enter_count(void)
{
    return crit_enters;
}
