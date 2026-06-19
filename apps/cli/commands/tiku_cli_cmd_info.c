/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_info.c - "info" command implementation
 *
 * Prints device name, CPU frequency, uptime, clock tick rate,
 * event queue usage, and active process count.
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

#include "tiku_cli_cmd_info.h"
#include <apps/cli/tiku_cli.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>

/*---------------------------------------------------------------------------*/
/* DEVICE NAME (compile-time)                                                */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_MSP430FR5969)
#define CLI_DEVICE_NAME "MSP430FR5969"
#elif defined(TIKU_DEVICE_MSP430FR5994)
#define CLI_DEVICE_NAME "MSP430FR5994"
#elif defined(TIKU_DEVICE_MSP430FR2433)
#define CLI_DEVICE_NAME "MSP430FR2433"
#else
#define CLI_DEVICE_NAME "Unknown"
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_cli_cmd_info(uint8_t argc, const char *argv[])
{
    unsigned long secs = tiku_clock_seconds();
    unsigned long hours = secs / 3600;
    unsigned long mins  = (secs % 3600) / 60;
    unsigned long s     = secs % 60;
    struct tiku_process *p;
    uint8_t proc_count = 0;

    (void)argc;
    (void)argv;

    for (p = tiku_process_list_head; p != NULL; p = p->next) {
        proc_count++;
    }

    CLI_PRINTF("Device:    %s\n", CLI_DEVICE_NAME);
    CLI_PRINTF("CPU:       %lu MHz\n", TIKU_MAIN_CPU_HZ / 1000000UL);
    CLI_PRINTF("Uptime:    %luh %lum %lus (%lu s)\n",
                hours, mins, s, secs);
    CLI_PRINTF("Clock:     %u ticks/sec (now %u)\n",
                (unsigned)TIKU_CLOCK_SECOND, tiku_clock_time());
    CLI_PRINTF("Queue:     %u/%u events\n",
                tiku_process_queue_length(), TIKU_QUEUE_SIZE);
    CLI_PRINTF("Processes: %u active\n", proc_count);
}
