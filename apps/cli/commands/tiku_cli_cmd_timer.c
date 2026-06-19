/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_timer.c - "timer" command implementation
 *
 * Prints whether any software timers are pending, the nearest
 * expiration time, and the remaining ticks.
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

#include "tiku_cli_cmd_timer.h"
#include <apps/cli/tiku_cli.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_htimer.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_cli_cmd_timer(uint8_t argc, const char *argv[])
{
    tiku_clock_time_t now  = tiku_clock_time();
    int pending            = tiku_timer_any_pending();
    tiku_clock_time_t next = tiku_timer_next_expiration();
    int hw_sched           = tiku_htimer_is_scheduled();

    (void)argc;
    (void)argv;

    CLI_PRINTF("Software timers:\n");
    CLI_PRINTF("  Pending:   %s\n", pending ? "yes" : "no");
    if (pending) {
        tiku_clock_time_t rem = next - now;
        CLI_PRINTF("  Next exp:  tick %u (in %u ticks)\n",
                    next, rem);
    }
    CLI_PRINTF("  Clock now: %u\n", now);

    CLI_PRINTF("Hardware timer:\n");
    CLI_PRINTF("  Scheduled: %s\n", hw_sched ? "yes" : "no");
}
