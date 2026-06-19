/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_kill.c - "kill" command implementation
 *
 * Terminates a process by posting TIKU_EVENT_FORCE_EXIT to it.
 * The PID is the 1-based index into the process list, matching
 * the output of the "ps" command.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_cli_cmd_kill.h"
#include <apps/cli/tiku_cli.h>             /* CLI_PRINTF via tiku_cli_io.h */
#include <kernel/process/tiku_process.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_cli_cmd_kill(uint8_t argc, const char *argv[])
{
    struct tiku_process *p;
    int8_t pid;
    uint8_t idx;

    if (argc < 2) {
        CLI_PRINTF("Usage: kill <pid>\n");
        CLI_PRINTF("Use 'ps' to list process IDs.\n");
        return;
    }

    /* Parse PID from argument (simple atoi for small integers) */
    pid = 0;
    for (idx = 0; argv[1][idx] != '\0'; idx++) {
        if (argv[1][idx] < '0' || argv[1][idx] > '9') {
            CLI_PRINTF("Error: invalid PID '%s'\n", argv[1]);
            return;
        }
        pid = pid * 10 + (argv[1][idx] - '0');
    }

    p = tiku_process_get(pid);
    if (p == NULL) {
        CLI_PRINTF("Error: no process with PID %d\n", pid);
        return;
    }

    /* Prevent killing the CLI process itself */
    if (p == &tiku_cli_process) {
        CLI_PRINTF("Error: cannot kill the CLI process\n");
        return;
    }

    CLI_PRINTF("Stopping process %d (%s)...\n", pid,
               p->name ? p->name : "(null)");
    tiku_process_stop(pid);
    CLI_PRINTF("Process %d stopped\n", pid);
}
