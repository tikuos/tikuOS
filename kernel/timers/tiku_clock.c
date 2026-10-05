/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock.c - System clock implementation
 *
 * Wrappers around the architecture-specific clock functions, plus weak
 * tickless-idle defaults for a port without that backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_clock.h"
#include <tiku.h>
#include <hal/tiku_compiler.h>   /* TIKU_WEAK (tickless defaults) */

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the system clock.
 *
 * Delegates to the architecture-specific tiku_clock_arch_init()
 * which configures the hardware timer peripheral (Timer A0 on
 * MSP430) for a periodic tick at TIKU_CLOCK_SECOND Hz.
 */
void tiku_clock_init(void)
{
    CLOCK_ARCH_PRINTF("Init\n");
    tiku_clock_arch_init();
    CLOCK_ARCH_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Return the current system clock time in ticks.
 *
 * The arch tick ISR advances the counter (Timer A0 on MSP430), and a
 * tickless wake adds the ticks it slept through.  The returned value
 * wraps at the maximum of tiku_clock_time_t.
 */
tiku_clock_time_t tiku_clock_time(void)
{
    return (tiku_clock_time_t)tiku_clock_arch_time();
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Return the number of whole seconds since boot.
 *
 * Returns the arch's seconds count, which most ports keep apart from the
 * tick counter so it does not wrap with it.
 */
unsigned long tiku_clock_seconds(void)
{
    return tiku_clock_arch_seconds();
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Busy-wait for @p t clock ticks.
 *
 * Spins until the tick counter has advanced by at least @p t.
 * Suitable for short delays; use software timers for longer waits.
 */
void tiku_clock_wait(tiku_clock_time_t t)
{
    tiku_clock_arch_wait((tiku_clock_arch_time_t)t);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Busy-wait for @p dt delay units.
 *
 * Microseconds on most ports; on MSP430 and RA8P1 a NOP-loop count whose
 * length depends on the CPU clock.  ISR jitter adds to any of them.
 */
void tiku_clock_delay_usec(unsigned int dt)
{
    tiku_clock_arch_delay(dt);
}

/*---------------------------------------------------------------------------*/

unsigned char tiku_clock_fault(void)
{
    return tiku_clock_arch_fault();
}

/*---------------------------------------------------------------------------*/
/* TICKLESS IDLE — weak defaults (no backend)                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Weak default: no tickless backend, never stretches.
 *
 * A platform with an always-on free-running time base overrides all three
 * symbols.  With the defaults the scheduler keeps per-tick wake-ups.
 */
TIKU_WEAK int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead)
{
    (void)ticks_ahead;
    return 0;
}

/** @brief Weak default: nothing to close. */
TIKU_WEAK void tiku_clock_tickless_end(void)
{
}

/** @brief Weak default: no backend present. */
TIKU_WEAK int tiku_clock_tickless_available(void)
{
    return 0;
}

/*---------------------------------------------------------------------------*/
