/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_info.c - "info" command implementation
 *
 * Prints device name, CPU frequency, uptime, clock tick rate,
 * event queue usage, and active process count.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_info.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_clock.h>
#include <hal/tiku_cpu.h>

/*---------------------------------------------------------------------------*/
/* DEVICE NAME (compile-time)                                                */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_NAME)
#define CLI_DEVICE_NAME TIKU_DEVICE_NAME
#else
#define CLI_DEVICE_NAME "Unknown"
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_info(uint8_t argc, const char *argv[])
{
    unsigned long secs = tiku_clock_seconds();
    unsigned long hours = secs / 3600;
    unsigned long mins  = (secs % 3600) / 60;
    unsigned long s     = secs % 60;

    (void)argc;
    (void)argv;

    SHELL_PRINTF("Device:    %s\n", CLI_DEVICE_NAME);
    SHELL_PRINTF("CPU:       %lu MHz\n", tiku_cpu_mclk_hz() / 1000000UL);
    SHELL_PRINTF("Uptime:    %luh %lum %lus (%lu s)\n",
                hours, mins, s, secs);
    SHELL_PRINTF("Clock:     %u ticks/sec (now %u)\n",
                (unsigned)TIKU_CLOCK_SECOND, tiku_clock_time());
    SHELL_PRINTF("Queue:     %u/%u events\n",
                tiku_process_queue_length(), TIKU_QUEUE_SIZE);
    SHELL_PRINTF("Processes: %u registered\n", tiku_process_count());
}
