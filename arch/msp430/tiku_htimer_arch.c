/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - MSP430 hardware timer architecture implementation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_htimer_arch.c
 * @brief MSP430FR5969 hardware timer implementation
 *
 * Timer A1 based hardware timer for the htimer subsystem.
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <tiku.h>
#include "tiku_htimer_arch.h"
#include <arch/msp430/tiku_compiler.h>
#include <msp430.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* INTERRUPT HANDLER                                                         */
/*---------------------------------------------------------------------------*/

TIKU_ISR(TIMER1_A0_VECTOR, tiku_htimer_isr)
{
    HTIMER_ARCH_PRINTF("Timer interrupt fired at %u\n",
                       tiku_htimer_arch_now());

    tiku_htimer_run_next();
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_htimer_arch_init(void)
{
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

    __enable_interrupt();
}

/*---------------------------------------------------------------------------*/

void tiku_htimer_arch_configure_aclk(void)
{
#if TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_ACLK

    HTIMER_ARCH_PRINTF("Configuring ACLK clock source\n");

#if TIKU_DEVICE_CS_HAS_KEY
    CSCTL0_H = CSKEY_H;
#endif

#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
    /* FR2433: ACLK source is in CSCTL4 (SELA bit) */
    #if TIKU_ACLK_CONFIG_SOURCE == TIKU_ACLK_SOURCE_VLOCLK
        /* FR2433 ACLK doesn't support VLO directly; use REFOCLK */
        CSCTL4 = (CSCTL4 & ~SELA__REFOCLK) | SELA__REFOCLK;
        HTIMER_ARCH_PRINTF("ACLK source: REFOCLK (FR2433 fallback)\n");
    #else
        CSCTL4 = (CSCTL4 & ~SELA__REFOCLK) | SELA__REFOCLK;
        HTIMER_ARCH_PRINTF("ACLK source: REFOCLK (default)\n");
    #endif
#else
    /* FR5969/FR5994: ACLK source is in CSCTL2 */
    #if TIKU_ACLK_CONFIG_SOURCE == TIKU_ACLK_SOURCE_VLOCLK
        CSCTL2 = (CSCTL2 & ~SELA_7) | SELA__VLOCLK;
        HTIMER_ARCH_PRINTF("ACLK source: VLOCLK (~10kHz)\n");
    #else
        CSCTL2 = (CSCTL2 & ~SELA_7) | SELA__VLOCLK;
        HTIMER_ARCH_PRINTF("ACLK source: VLOCLK (default)\n");
    #endif
#endif

#if TIKU_DEVICE_CS_HAS_KEY
    CSCTL0_H = 0;
#endif

#endif /* TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_ACLK */
}

/*---------------------------------------------------------------------------*/

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

void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    HTIMER_ARCH_PRINTF("Scheduling hardware interrupt: target=%u, now=%u\n",
                       t, tiku_htimer_arch_now());

    TA1CCR0 = t;

    TA1CCTL0 &= ~CCIFG;

    TA1CCTL0 |= CCIE;
}

/*---------------------------------------------------------------------------*/

void tiku_htimer_arch_disable_interrupt(void)
{
    TA1CCTL0 &= ~CCIE;
    HTIMER_ARCH_PRINTF("Hardware timer interrupts disabled\n");
}

/*---------------------------------------------------------------------------*/

void tiku_htimer_arch_enable_interrupt(void)
{
    TA1CCTL0 |= CCIE;
    HTIMER_ARCH_PRINTF("Hardware timer interrupts enabled\n");
}

/*---------------------------------------------------------------------------*/

int tiku_htimer_arch_interrupt_pending(void)
{
    return (TA1CCTL0 & CCIFG) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/

unsigned int tiku_htimer_arch_get_timer_config(void)
{
    return TA1CTL;
}

/*---------------------------------------------------------------------------*/

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

void tiku_htimer_arch_reset_counter(void)
{
    __disable_interrupt();
    TA1CTL |= TACLR;
    __enable_interrupt();
    HTIMER_ARCH_PRINTF("Timer counter reset to 0\n");
}

/*---------------------------------------------------------------------------*/
