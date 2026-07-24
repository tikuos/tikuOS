/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ps.c - "ps" command implementation
 *
 * Walks the process linked list and prints each active process
 * with its name and running status.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_ps.h"
#include <kernel/shell/tiku_shell.h>             /* SHELL_PRINTF via tiku_shell_io.h */
#include <kernel/process/tiku_process.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_ps(uint8_t argc, const char *argv[])
{
    struct tiku_process *p;
    uint8_t i;

    (void)argc;
    (void)argv;

    SHELL_PRINTF("PID  STATE     SRAM  FRAM  NAME\n");
    SHELL_PRINTF("---  --------  ----  ----  ---------------\n");

    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        p = tiku_process_get((int8_t)i);
        if (p == NULL) {
            continue;
        }
        SHELL_PRINTF("%3u  %-8s %6lu %6lu  %s\n",
                   i,
                   tiku_process_state_str(p->state),
                   (unsigned long)tiku_process_sram_used(p),
                   (unsigned long)tiku_process_fram_used(p),
                   p->name ? p->name : "(null)");
    }

    SHELL_PRINTF("---\n");
    SHELL_PRINTF("%u process(es) registered\n", tiku_process_count());
    SHELL_PRINTF("Event queue: %u/%u\n",
                tiku_process_queue_length(), TIKU_QUEUE_SIZE);
}
