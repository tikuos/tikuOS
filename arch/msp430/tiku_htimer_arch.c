/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - MSP430 hardware timer architecture implementation
 *
 * Timer A1-based single-shot compare-match timer for the htimer
 * subsystem.  Runs in continuous mode; the CCR0 compare interrupt runs
 * the next htimer through tiku_htimer_run_next().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_htimer_arch.h"
#include <hal/tiku_compiler.h>
#include <msp430.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* INTERRUPT HANDLER                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Timer A1 CCR0 compare ISR: runs the next htimer. */
TIKU_ISR(TIMER1_A0_VECTOR, tiku_htimer_isr)
{
    HTIMER_ARCH_PRINTF("Timer interrupt fired at %u\n",
                       tiku_htimer_arch_now());

    tiku_htimer_run_next();
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure Timer A1 for continuous-mode compare-match operation.
 *
 * Sets the clock source, dividers, and enables the CCR0 interrupt.
 * Interrupts are off while it runs and the caller's GIE state is restored
 * on return; tiku_sched_loop() is what enables GIE.
 */
void tiku_htimer_arch_init(void)
{
    unsigned int sr = __get_interrupt_state();
    __disable_interrupt();

    HTIMER_ARCH_PRINTF("Initializing Timer A1 for hardware timer\n");

    TA1CTL = 0;

    TA1CTL = TIKU_HTIMER_TASSEL_VALUE |
             TIKU_HTIMER_ID_VALUE |
             MC__CONTINUOUS |
             TACLR;

    TA1EX0 = TIKU_HTIMER_TAIDEX_VALUE;

    TA1CCTL0 = CCIE;

    TA1CCTL0 &= ~CCIFG;

    HTIMER_ARCH_PRINTF("Timer A1 initialization complete\n");

    /* Restore the caller's GIE state. */
    __set_interrupt_state(sr);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Preserve the shared ACLK source selected by the system clock.
 *
 * This compatibility hook performs no register writes.
 */
void tiku_htimer_arch_configure_aclk(void)
{
    /* ACLK is shared with Timer A0 and configured by tiku_clock_arch_init. */
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Read the current Timer A1 counter with double-read stability.
 *
 * When the timer clock is asynchronous to MCLK, one read of TA1R can catch
 * it mid-update.  The loop reads it twice until both reads match.
 */
tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    tiku_htimer_clock_t t1, t2;

    do {
        t1 = TA1R;
        t2 = TA1R;
    } while (t1 != t2);

    return t1;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Arm the Timer A1 CCR0 compare match for time @p t.
 *
 * Writes the target value to TA1CCR0, clears any pending interrupt
 * flag, and enables the compare interrupt.
 */
void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    HTIMER_ARCH_PRINTF("Scheduling hardware interrupt: target=%u, now=%u\n",
                       t, tiku_htimer_arch_now());

    TA1CCR0 = t;

    TA1CCTL0 &= ~CCIFG;

    TA1CCTL0 |= CCIE;
}

/*---------------------------------------------------------------------------*/

/** @brief Disable the Timer A1 CCR0 compare interrupt. */
void tiku_htimer_arch_disable_interrupt(void)
{
    TA1CCTL0 &= ~CCIE;
    HTIMER_ARCH_PRINTF("Hardware timer interrupts disabled\n");
}

/*---------------------------------------------------------------------------*/

/** @brief Enable the Timer A1 CCR0 compare interrupt. */
void tiku_htimer_arch_enable_interrupt(void)
{
    TA1CCTL0 |= CCIE;
    HTIMER_ARCH_PRINTF("Hardware timer interrupts enabled\n");
}

/*---------------------------------------------------------------------------*/

/** @brief Return non-zero if a Timer A1 CCR0 interrupt is pending. */
int tiku_htimer_arch_interrupt_pending(void)
{
    return (TA1CCTL0 & CCIFG) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/

/** @brief Return the raw TA1CTL register value for diagnostics. */
unsigned int tiku_htimer_arch_get_timer_config(void)
{
    return TA1CTL;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Print the full htimer configuration to debug output.
 *
 * Prints through HTIMER_ARCH_PRINTF, which is empty unless DEBUG_HTIMER
 * is set.
 */
void tiku_htimer_arch_print_config(void)
{
    tiku_htimer_config_t config;
    tiku_htimer_get_config(&config);

    HTIMER_ARCH_PRINTF("Htimer Configuration:\n");

    HTIMER_ARCH_PRINTF("  Clock source: ");
    switch(config.clock_source) {
        case TIKU_HTIMER_SOURCE_SMCLK:
            HTIMER_ARCH_PRINTF("SMCLK\n");
            break;
        case TIKU_HTIMER_SOURCE_ACLK:
            HTIMER_ARCH_PRINTF("ACLK\n");
            break;
        case TIKU_HTIMER_SOURCE_EXTERNAL:
            HTIMER_ARCH_PRINTF("External\n");
            break;
        case TIKU_HTIMER_SOURCE_INCLK:
            HTIMER_ARCH_PRINTF("INCLK\n");
            break;
    }

    if (config.clock_source == TIKU_HTIMER_SOURCE_ACLK) {
        HTIMER_ARCH_PRINTF("  ACLK source: ");
        switch(config.aclk_source) {
            case TIKU_ACLK_SOURCE_VLOCLK:
                HTIMER_ARCH_PRINTF("VLOCLK (~10kHz, varies)\n");
                break;
            case TIKU_ACLK_SOURCE_XT1CLK:
                HTIMER_ARCH_PRINTF("XT1CLK (32.768kHz crystal)\n");
                break;
            case TIKU_ACLK_SOURCE_REFOCLK:
                HTIMER_ARCH_PRINTF("REFOCLK (32.768kHz internal)\n");
                break;
        }
    }

    HTIMER_ARCH_PRINTF("  Primary divider: /%d\n",
                       TIKU_HTIMER_DIV_VALUE);
    HTIMER_ARCH_PRINTF("  Extended divider: /%d\n",
                       TIKU_HTIMER_EXDIV_VALUE);
    HTIMER_ARCH_PRINTF("  Total divider: /%d\n",
                       TIKU_HTIMER_DIV_VALUE * TIKU_HTIMER_EXDIV_VALUE);
    HTIMER_ARCH_PRINTF("  Base frequency: %d Hz\n",
                       config.base_frequency);
    HTIMER_ARCH_PRINTF("  Timer frequency: %d Hz\n",
                       config.timer_frequency);
    HTIMER_ARCH_PRINTF("  Timer period: %d us\n",
                       1000000L / config.timer_frequency);

    if (config.timer_frequency >= 1000000) {
        HTIMER_ARCH_PRINTF("  Resolution: %d ns\n",
                           1000000000L / config.timer_frequency);
    } else {
        HTIMER_ARCH_PRINTF("  Resolution: %d us\n",
                           1000000L / config.timer_frequency);
    }
    HTIMER_ARCH_PRINTF("  Max delay: %d ms\n",
                       65535000L / config.timer_frequency);
}

/*---------------------------------------------------------------------------*/

/** @brief Reset Timer A1 to zero (TACLR), preserving the caller's GIE. */
void tiku_htimer_arch_reset_counter(void)
{
    uint16_t gie = __get_SR_register() & GIE;
    __disable_interrupt();
    TA1CTL |= TACLR;
    if (gie != 0u) {
        __enable_interrupt();
    }
    HTIMER_ARCH_PRINTF("Timer counter reset to 0\n");
}

/*---------------------------------------------------------------------------*/
