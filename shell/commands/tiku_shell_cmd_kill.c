/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_kill.c - "kill" command implementation
 *
 * Stops a process with tiku_process_stop(): it stays registered, and `resume`
 * continues it where it last yielded.  The PID is the registry slot that "ps"
 * shows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_kill.h"
#include <shell/tiku_shell.h>    /* SHELL_PRINTF via tiku_shell_io.h */
#include <kernel/process/tiku_process.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_kill(uint8_t argc, const char *argv[])
{
    struct tiku_process *p;
    int8_t pid;
    size_t idx;

    if (argc < 2) {
        SHELL_PRINTF("Usage: kill <pid>\n");
        SHELL_PRINTF("Use 'ps' to list process IDs.\n");
        return;
    }

    /* Parse the PID: decimal digits only. */
    pid = 0;
    if (argv[1][0] == '\0') {
        SHELL_PRINTF("Error: invalid PID '%s'\n", argv[1]);
        return;
    }
    for (idx = 0; argv[1][idx] != '\0'; idx++) {
        if (argv[1][idx] < '0' || argv[1][idx] > '9') {
            SHELL_PRINTF("Error: invalid PID '%s'\n", argv[1]);
            return;
        }
        if (pid > (INT8_MAX - (argv[1][idx] - '0')) / 10) {
            SHELL_PRINTF("Error: invalid PID '%s'\n", argv[1]);
            return;
        }
        pid = pid * 10 + (argv[1][idx] - '0');
    }

    p = tiku_process_get(pid);
    if (p == NULL) {
        SHELL_PRINTF("Error: no process with PID %d\n", pid);
        return;
    }

    /* Prevent killing the CLI process itself */
    if (p == &tiku_shell_process) {
        SHELL_PRINTF("Error: cannot kill the CLI process\n");
        return;
    }

    SHELL_PRINTF("Stopping process %d (%s)...\n", pid,
               p->name ? p->name : "(null)");
    tiku_process_stop(pid);
    SHELL_PRINTF("Process %d stopped\n", pid);
}
