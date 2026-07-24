/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_timer.c - "timer" command implementation
 *
 * Prints whether any software timers are pending, the nearest
 * expiration time, and the remaining ticks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_timer.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_htimer.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_timer(uint8_t argc, const char *argv[])
{
    tiku_clock_time_t now  = tiku_clock_time();
    int pending            = tiku_timer_any_pending();
    tiku_clock_time_t next = tiku_timer_next_expiration();
    int hw_sched           = tiku_htimer_is_scheduled();

    (void)argc;
    (void)argv;

    SHELL_PRINTF("Software timers:\n");
    SHELL_PRINTF("  Pending:   %s\n", pending ? "yes" : "no");
    if (pending) {
        tiku_clock_time_t rem = next - now;
        SHELL_PRINTF("  Next exp:  tick %u (in %u ticks)\n",
                    next, rem);
    }
    SHELL_PRINTF("  Clock now: %u\n", now);

    SHELL_PRINTF("Hardware timer:\n");
    SHELL_PRINTF("  Scheduled: %s\n", hw_sched ? "yes" : "no");
}
