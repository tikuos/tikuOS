/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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

void tiku_clock_init(void)
{
    CLOCK_ARCH_PRINTF("Init\n");
    tiku_clock_arch_init();
    CLOCK_ARCH_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

tiku_clock_time_t tiku_clock_time(void)
{
    return (tiku_clock_time_t)tiku_clock_arch_time();
}

/*---------------------------------------------------------------------------*/

unsigned long tiku_clock_seconds(void)
{
    return tiku_clock_arch_seconds();
}

/*---------------------------------------------------------------------------*/

void tiku_clock_wait(tiku_clock_time_t t)
{
    tiku_clock_arch_wait((tiku_clock_arch_time_t)t);
}

/*---------------------------------------------------------------------------*/

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
/* TICKLESS IDLE DEFAULTS                                                    */
/*---------------------------------------------------------------------------*/

TIKU_WEAK int tiku_clock_tickless_begin(tiku_clock_time_t ticks_ahead)
{
    (void)ticks_ahead;
    return 0;
}

TIKU_WEAK void tiku_clock_tickless_end(void)
{
}

TIKU_WEAK int tiku_clock_tickless_available(void)
{
    return 0;
}

/*---------------------------------------------------------------------------*/
