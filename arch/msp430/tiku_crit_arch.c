/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - MSP430 IRQ-mask backend for tiku_crit.
 *
 * Implements the two hooks in hal/tiku_crit_hal.h.  Each register is masked
 * only where its OFS_<reg> macro exists, so one file serves the FR5969,
 * FR5994, FR6989 and FR2433.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/*
 * Interrupt-enable state saved by mask and restored by unmask: each field
 * holds its register's value from before the mask cleared it.  One static
 * instance serves because tiku_crit windows do not nest (the kernel
 * refuses re-entry with TIKU_CRIT_ERR_BUSY).
 *
 * The guards test defined(OFS_<reg>), a macro that exists exactly for the
 * registers the selected device has.  The register names (PxIE, UCxIE,
 * ADC12IER0, SFRIE1) are sfr_b()/sfr_w() symbols, not macros, so
 * defined(UCA0IE) is false under msp430-elf-gcc even where the register
 * exists, and a guard on it compiles the mask out.
 */
static struct {
    uint16_t ta0_ccie;
    uint16_t ta1_ccie;
#if defined(OFS_UCA0IE)
    uint16_t uca0_ie;
#endif
#if defined(OFS_UCA1IE)
    uint16_t uca1_ie;
#endif
#if defined(OFS_UCB0IE)
    uint16_t ucb0_ie;
#endif
#if defined(OFS_UCB1IE)
    uint16_t ucb1_ie;
#endif
#if defined(OFS_ADC12IER0)
    uint16_t adc12_ier0;
#endif
#if defined(OFS_SFRIE1)
    uint8_t  sfrie1_wdtie;
#endif
#if defined(OFS_P1IE)
    uint8_t  p1_ie;
#endif
#if defined(OFS_P2IE)
    uint8_t  p2_ie;
#endif
#if defined(OFS_P3IE)
    uint8_t  p3_ie;
#endif
#if defined(OFS_P4IE)
    uint8_t  p4_ie;
#endif
} crit_ie_saved;

/*---------------------------------------------------------------------------*/
/* HAL ENTRY POINTS                                                          */
/*---------------------------------------------------------------------------*/

/*
 * Clears the enable bit of every interrupt source preserve_mask does not
 * name, saving each register first.  GIE stays set, so a preserved ISR
 * (typically the bit clock) keeps firing.  Each register is touched at
 * most once, and only the GPIO ports 1-4 are masked.
 */
void
tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
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

#if defined(OFS_UCA0IE) || defined(OFS_UCA1IE)
    /* eUSCI_A: UART (and SPI when configured as such) */
#if defined(OFS_UCA0IE)
    crit_ie_saved.uca0_ie = UCA0IE;
#endif
#if defined(OFS_UCA1IE)
    crit_ie_saved.uca1_ie = UCA1IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_UART)) {
#if defined(OFS_UCA0IE)
        UCA0IE = 0;
#endif
#if defined(OFS_UCA1IE)
        UCA1IE = 0;
#endif
    }
#endif

#if defined(OFS_UCB0IE) || defined(OFS_UCB1IE)
    /* eUSCI_B: I2C and SPI */
#if defined(OFS_UCB0IE)
    crit_ie_saved.ucb0_ie = UCB0IE;
#endif
#if defined(OFS_UCB1IE)
    crit_ie_saved.ucb1_ie = UCB1IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_I2C)) {
#if defined(OFS_UCB0IE)
        UCB0IE = 0;
#endif
#if defined(OFS_UCB1IE)
        UCB1IE = 0;
#endif
    }
#endif

#if defined(OFS_ADC12IER0)
    /* ADC12 done */
    crit_ie_saved.adc12_ier0 = ADC12IER0;
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_ADC)) {
        ADC12IER0 = 0;
    }
#endif

#if defined(OFS_SFRIE1) && defined(WDTIE)
    /* Watchdog interval-mode IRQ.  The watchdog reset is not an
     * interrupt and is not masked here. */
    crit_ie_saved.sfrie1_wdtie = SFRIE1 & WDTIE;
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_WDT)) {
        SFRIE1 &= ~WDTIE;
    }
#endif

    /* External GPIO edge ISRs */
#if defined(OFS_P1IE)
    crit_ie_saved.p1_ie = P1IE;
#endif
#if defined(OFS_P2IE)
    crit_ie_saved.p2_ie = P2IE;
#endif
#if defined(OFS_P3IE)
    crit_ie_saved.p3_ie = P3IE;
#endif
#if defined(OFS_P4IE)
    crit_ie_saved.p4_ie = P4IE;
#endif
    if (!(preserve_mask & TIKU_CRIT_PRESERVE_GPIO)) {
#if defined(OFS_P1IE)
        P1IE = 0;
#endif
#if defined(OFS_P2IE)
        P2IE = 0;
#endif
#if defined(OFS_P3IE)
        P3IE = 0;
#endif
#if defined(OFS_P4IE)
        P4IE = 0;
#endif
    }
}

/*
 * Restores every enable bit the mask saved.  Lost interrupts in the
 * masked window:
 *   - Timer A0: CCIFG latches only one missed tick, so several missed compares
 *     collapse into a single post-window ISR and the tick count slips.
 *   - UART RX: bytes beyond the 1-byte hardware buffer are lost, though the
 *     most recent one may still raise an ISR after restore.
 *   - GPIO: PxIFG latches one edge per pin, so several edges collapse to one.
 *   - ADC / I2C / WDT: completion flags survive in hardware and fire once.
 */
void
tiku_crit_arch_unmask_irqs(void)
{
    TA0CCTL0 = (TA0CCTL0 & ~CCIE) | crit_ie_saved.ta0_ccie;
    TA1CCTL0 = (TA1CCTL0 & ~CCIE) | crit_ie_saved.ta1_ccie;

#if defined(OFS_UCA0IE)
    UCA0IE = crit_ie_saved.uca0_ie;
#endif
#if defined(OFS_UCA1IE)
    UCA1IE = crit_ie_saved.uca1_ie;
#endif

#if defined(OFS_UCB0IE)
    UCB0IE = crit_ie_saved.ucb0_ie;
#endif
#if defined(OFS_UCB1IE)
    UCB1IE = crit_ie_saved.ucb1_ie;
#endif

#if defined(OFS_ADC12IER0)
    ADC12IER0 = crit_ie_saved.adc12_ier0;
#endif

#if defined(OFS_SFRIE1) && defined(WDTIE)
    SFRIE1 = (SFRIE1 & ~WDTIE) | crit_ie_saved.sfrie1_wdtie;
#endif

#if defined(OFS_P1IE)
    P1IE = crit_ie_saved.p1_ie;
#endif
#if defined(OFS_P2IE)
    P2IE = crit_ie_saved.p2_ie;
#endif
#if defined(OFS_P3IE)
    P3IE = crit_ie_saved.p3_ie;
#endif
#if defined(OFS_P4IE)
    P4IE = crit_ie_saved.p4_ie;
#endif
}
