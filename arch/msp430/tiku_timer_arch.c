/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - MSP430 timer architecture implementation
 *
 * The system clock: Timer A0 from ACLK raises one interrupt per tick, which
 * advances the tick and second counts and polls the timer process.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_timer_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <tiku.h>
#include <hal/tiku_clock_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <hal/tiku_compiler.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* DEBUG CONFIGURATION                                                       */
/*---------------------------------------------------------------------------*/

#if DEBUG_CLOCK_ARCH
#define CLOCK_PRINTF(...) TIKU_PRINTF("[CLOCK_ARCH] " __VA_ARGS__)
#else
#define CLOCK_PRINTF(...)
#endif

/*---------------------------------------------------------------------------*/
/* CS MODULE ABSTRACTION                                                     */
/*---------------------------------------------------------------------------*/

#if TIKU_DEVICE_CS_HAS_KEY
#define TIKU_CS_UNLOCK()    do { CSCTL0_H = CSKEY_H; } while(0)
#define TIKU_CS_LOCK()      do { CSCTL0_H = 0; } while(0)
#else
#define TIKU_CS_UNLOCK()    do { } while(0)
#define TIKU_CS_LOCK()      do { } while(0)
#endif

/*---------------------------------------------------------------------------*/
/* CONFIGURATION CHECKS                                                      */
/*---------------------------------------------------------------------------*/

/* TIKU_CLOCK_ARCH_CONF_SECOND must be a power of two: the ISR's modulo then
 * compiles to a mask. */
#if (TIKU_CLOCK_ARCH_CONF_SECOND & (TIKU_CLOCK_ARCH_CONF_SECOND - 1)) != 0
#error TIKU_CLOCK_ARCH_CONF_SECOND must be a power of two (e.g., 128, 256)
#endif

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/* Half the tick range, and a wrap-safe a < b on 16-bit counts (unused in
 * this file). */
#define TIKU_ARCH_MAX_TICKS (~((tiku_clock_arch_time_t)0) / 2)
#define TIKU_CLOCK_ARCH_LT(a, b) ((signed short)((a)-(b)) < 0)

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/* Seconds and ticks since init, TA0R at the last tick, and the ACLK fault
 * code tiku_clock_arch_fault() returns. */
static volatile unsigned long tiku_arch_seconds = 0;
static volatile tiku_clock_arch_time_t tiku_arch_count = 0;
static volatile unsigned short tiku_arch_last_tar = 0;
static unsigned char tiku_arch_clock_fault = TIKU_CLOCK_ARCH_FAULT_NONE;

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
 * @brief Configure clock source for Timer A0.
 *
 * Parts with LFXT run ACLK from the 32.768 kHz crystal, or from VLOCLK with
 * fault code TIKU_CLOCK_ARCH_FAULT_LFXT_VLO when it still faults after 50000
 * polls.  The FR2433 runs ACLK from REFOCLK (32.768 kHz internal).
 */
static void tiku_configure_aclk_source(void)
{
#if TIKU_DEVICE_HAS_LFXT
    /* Configure XT1 crystal oscillator (32.768 kHz) */
    TIKU_DEVICE_LFXT_PSEL_REG |= TIKU_DEVICE_LFXT_PSEL_BITS;

    TIKU_CS_UNLOCK();

    CSCTL4 &= ~LFXTOFF;
    CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_3; /* Max drive for startup */

    unsigned int xt1_timeout = 0;
    do {
        CSCTL5 &= ~LFXTOFFG;
        SFRIFG1 &= ~OFIFG;
        __delay_cycles(10000); /* Give crystal time to settle */
        if (++xt1_timeout > 50000U) {
            CLOCK_PRINTF("XT1 fault timeout, falling back to VLO\n");
            CSCTL4 |= LFXTOFF;
            CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
            g_aclk_hz = VLO_FREQ_NOMINAL_HZ;
            TIKU_CS_LOCK();
            tiku_arch_clock_fault = TIKU_CLOCK_ARCH_FAULT_LFXT_VLO;
            return;
        }
    } while (CSCTL5 & LFXTOFFG);

    CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_0; /* Low drive once stable */

    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    g_aclk_hz = XT1_FREQ_32KHZ;

    TIKU_CS_LOCK();

    CLOCK_PRINTF("XT1 crystal configured (32.768 kHz)\n");

#else
    /* No crystal: ACLK runs from REFOCLK (32.768 kHz internal reference)
     * and MCLK and SMCLK stay on DCOCLKDIV. */
    TIKU_CS_UNLOCK();

    CSCTL4 = SELA__REFOCLK | SELMS__DCOCLKDIV;
    g_aclk_hz = REFO_FREQ_HZ;

    TIKU_CS_LOCK();

    CLOCK_PRINTF("REFOCLK configured for ACLK (32.768 kHz internal)\n");
#endif
}

/*---------------------------------------------------------------------------*/
/* INTERRUPT HANDLER                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Timer A0 CCR0 ISR: one system tick.
 *
 * Advances the tick and second counts, polls the timer process outside a
 * tiku_crit window, and leaves LPM3 on exit.
 */
TIKU_ISR(TIMER0_A0_VECTOR, timer0_a0_isr)
{
    tiku_arch_last_tar = TA0R;

    ++tiku_arch_count;

    if ((tiku_arch_count % TIKU_CLOCK_ARCH_CONF_SECOND) == 0) {
        ++tiku_arch_seconds;
    }

    /* During a tiku_crit window, suppress the timer-process poll so
     * the dispatcher does not run between bit-bang edges. The tick
     * counter still advances; the catch-up poll fires on
     * tiku_crit_end(). */
    if (!tiku_crit_active()) {
        tiku_timer_request_poll();
    }

    __bic_SR_register_on_exit(LPM3_bits);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_clock_arch_init(void)
{
    unsigned int state;

    CLOCK_PRINTF("Initializing system clock architecture\n");

    tiku_arch_clock_fault = TIKU_CLOCK_ARCH_FAULT_NONE;

    CLOCK_PRINTF("Configuring ACLK source\n");
    tiku_configure_aclk_source();

    /* Interrupts are off while Timer A0 is set up, and the caller's GIE
     * state comes back afterwards; the application or test runner decides
     * when to set GIE.  TA0R counts whatever GIE is; only the CCR0 ISR
     * needs it. */
    state = __get_interrupt_state();
    __disable_interrupt();

    TA0CTL = 0;

    TA0CTL = TASSEL_1 | MC_0 | TACLR;

    TA0CCR0 = TIKU_CLOCK_ARCH_INTERVAL - 1;

    TA0CCTL0 = CCIE;

    tiku_arch_count = 0;
    tiku_arch_seconds = 0;
    tiku_arch_last_tar = 0;

    TA0CTL |= MC_1;

    __set_interrupt_state(state);

    /* Two TA0R reads 10000 cycles apart show the counter running; only
     * DEBUG_CLOCK_ARCH builds print them.  The reads and the delay run in
     * every build, so every build spends the same time here after init;
     * the (void) casts cover builds where CLOCK_PRINTF is empty. */
    unsigned short tar1 = TA0R;
    __delay_cycles(10000);
    unsigned short tar2 = TA0R;

    CLOCK_PRINTF("Timer verification: TAR changed from %d to %d\n",
                 tar1, tar2);
    (void)tar1;
    (void)tar2;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Get current clock time in ticks
 *
 * Reads the tick count twice until both reads agree, so a tick that lands
 * mid-read cannot tear the 32-bit value.
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
 * Each unit is four NOPs plus the loop overhead, so its length depends on
 * MCLK and on the code the compiler emits for the loop.
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
 * Returns the Timer A0 count since the last tick (TA0R minus its value at
 * that tick).
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

/**
 * @brief Return the active clock-source fault code.
 *
 * Set when XT1 fails to start and ACLK is rerouted to VLOCLK, after which the
 * tick runs about three times slower than TIKU_CLOCK_SECOND implies.  Cleared
 * on each clock init.
 */
unsigned char tiku_clock_arch_fault(void)
{
    return tiku_arch_clock_fault;
}

/*---------------------------------------------------------------------------*/
