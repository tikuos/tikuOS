/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - MSP430 timer architecture implementation
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
 * @file tiku_timer_arch.c
 * @brief MSP430FR5969 architecture-specific clock implementation
 *
 * System clock using Timer A0 on the MSP430FR5969. Provides tick
 * counting, delays, and time measurement functionality.
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_timer_arch.h"
#include <tiku.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* DEBUG CONFIGURATION                                                       */
/*---------------------------------------------------------------------------*/

#if DEBUG_CLOCK_ARCH
#define CLOCK_PRINTF(...) printf("[CLOCK_ARCH] " __VA_ARGS__)
#else
#define CLOCK_PRINTF(...)
#endif

/*---------------------------------------------------------------------------*/
/* CONFIGURATION CHECKS                                                      */
/*---------------------------------------------------------------------------*/

/* Ensure CLOCK_SECOND is power of 2 for efficient modulo operation */
#if (TIKU_CLOCK_ARCH_CONF_SECOND & (TIKU_CLOCK_ARCH_CONF_SECOND - 1)) != 0
#error TIKU_CLOCK_ARCH_CONF_SECOND must be a power of two (e.g., 128, 256)
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_ARCH_MAX_TICKS (~((tiku_clock_arch_time_t)0) / 2)
#define TIKU_CLOCK_ARCH_LT(a, b) ((signed short)((a)-(b)) < 0)

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

static volatile unsigned long tiku_arch_seconds = 0;
static volatile tiku_clock_arch_time_t tiku_arch_count = 0;
static volatile unsigned short tiku_arch_last_tar = 0;

/*---------------------------------------------------------------------------*/
/* INTERNAL FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read timer register with double-read for stability
 * @return Stable timer value
 */
static inline unsigned short
tiku_arch_read_tar(void)
{
    unsigned short t1, t2;

    do {
        t1 = TA0R;
        t2 = TA0R;
    } while(t1 != t2);

    return t1;
}

/**
 * @brief Configure XT1 crystal oscillator (32.768 kHz)
 *
 * The MSP430FR5969 LaunchPad has a 32.768 kHz crystal connected
 * to PJ.4 (XIN) and PJ.5 (XOUT).
 */
static void tiku_configure_xt1_crystal(void)
{
    TIKU_DEVICE_LFXT_PSEL_REG |= TIKU_DEVICE_LFXT_PSEL_BITS;

    CSCTL0_H = CSKEY >> 8;

    CSCTL4 &= ~LFXTOFF;

    unsigned int xt1_timeout = 0;
    do {
        CSCTL5 &= ~LFXTOFFG;
        SFRIFG1 &= ~OFIFG;
        if (++xt1_timeout > 50000U) {
            CLOCK_PRINTF("XT1 fault timeout, falling back to VLO\n");
            CSCTL4 |= LFXTOFF;
            CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
            CSCTL0_H = 0;
            return;
        }
    } while (SFRIFG1 & OFIFG);

    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;

    CSCTL0_H = 0;

    CLOCK_PRINTF("XT1 crystal configured (32.768 kHz)\n");
}

/*---------------------------------------------------------------------------*/
/* INTERRUPT HANDLER                                                         */
/*---------------------------------------------------------------------------*/

/** Timer A0 CCR0 interrupt service routine */
#pragma vector=TIMER0_A0_VECTOR
__interrupt void timer0_a0_isr(void)
{
    tiku_arch_last_tar = TA0R;

    ++tiku_arch_count;

    if ((tiku_arch_count % TIKU_CLOCK_ARCH_CONF_SECOND) == 0) {
        ++tiku_arch_seconds;
    }

    tiku_timer_request_poll();    

    __bic_SR_register_on_exit(LPM3_bits);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_clock_arch_init(void)
{
    CLOCK_PRINTF("Initializing system clock architecture\n");

    CLOCK_PRINTF("Configuring XT1 crystal oscillator\n");
    tiku_configure_xt1_crystal();

    __disable_interrupt();

    TA0CTL = 0;

    TA0CTL = TASSEL_1 | MC_0 | TACLR;

    TA0CCR0 = TIKU_CLOCK_ARCH_INTERVAL - 1;

    TA0CCTL0 = CCIE;

    tiku_arch_count = 0;
    tiku_arch_seconds = 0;
    tiku_arch_last_tar = 0;

    TA0CTL |= MC_1;

    __enable_interrupt();

    unsigned short tar1 = TA0R;
    __delay_cycles(10000);
    unsigned short tar2 = TA0R;

    CLOCK_PRINTF("Timer verification: TAR changed from %d to %d\n",
                 tar1, tar2);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get current clock time in ticks
 *
 * Atomic read of the volatile tick counter.
 */
tiku_clock_arch_time_t tiku_clock_arch_time(void)
{
    tiku_clock_arch_time_t t1, t2;

    do {
        t1 = tiku_arch_count;
        t2 = tiku_arch_count;
    } while(t1 != t2);

    return t1;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get current time in seconds
 */
unsigned long tiku_clock_arch_seconds(void)
{
    unsigned long t1, t2;

    do {
        t1 = tiku_arch_seconds;
        t2 = tiku_arch_seconds;
    } while(t1 != t2);

    return t1;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Set the system time
 */
void tiku_clock_arch_set(tiku_clock_arch_time_t clock,
                         tiku_clock_arch_time_t fclock)
{
    unsigned int state;

    state = __get_interrupt_state();
    __disable_interrupt();

    TA0R = fclock;
    tiku_arch_count = clock;

    __set_interrupt_state(state);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Set the seconds counter
 */
void tiku_clock_arch_set_seconds(unsigned long sec)
{
    unsigned int state;

    state = __get_interrupt_state();
    __disable_interrupt();

    tiku_arch_seconds = sec;

    __set_interrupt_state(state);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified clock ticks
 *
 * Busy-waits for the specified number of system ticks.
 */
void tiku_clock_arch_wait(tiku_clock_arch_time_t t)
{
    tiku_clock_arch_time_t start;

    start = tiku_clock_arch_time();
    while((tiku_clock_arch_time() - start) < t);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief CPU delay loop
 *
 * Each unit is approximately 2.83us at 8MHz CPU clock.
 */
void tiku_clock_arch_delay(unsigned int i)
{
    while(i--) {
        __no_operation();
        __no_operation();
        __no_operation();
        __no_operation();
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get fine-grained clock value
 *
 * Returns timer counter value within current tick period.
 */
unsigned short tiku_clock_arch_fine(void)
{
    unsigned short t;

    t = tiku_arch_last_tar;

    return (unsigned short)(TA0R - t);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get maximum fine clock value
 */
int tiku_clock_arch_fine_max(void)
{
    return TIKU_CLOCK_ARCH_INTERVAL;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get raw timer counter value
 */
tiku_clock_arch_counter_t tiku_clock_arch_counter(void)
{
    return tiku_arch_read_tar();
}

/*---------------------------------------------------------------------------*/
