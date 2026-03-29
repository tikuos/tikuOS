/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_resume.c - "resume" command implementation
 *
 * Resumes a stopped process by calling tiku_process_resume().
 * The PID is the 0-based registry index, matching the output
 * of the "ps" command.
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

#include "tiku_shell_cmd_resume.h"
#include <kernel/shell/tiku_shell.h>             /* SHELL_PRINTF via tiku_shell_io.h */
#include <kernel/process/tiku_process.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_resume(uint8_t argc, const char *argv[])
{
    struct tiku_process *p;
    int8_t pid;
    uint8_t idx;

    if (argc < 2) {
        SHELL_PRINTF("Usage: resume <pid>\n");
        SHELL_PRINTF("Use 'ps' to list process IDs.\n");
        return;
    }

    /* Parse PID from argument (simple atoi for small integers) */
    pid = 0;
    for (idx = 0; argv[1][idx] != '\0'; idx++) {
        if (argv[1][idx] < '0' || argv[1][idx] > '9') {
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

    if (p->state != TIKU_PROCESS_STATE_STOPPED) {
        SHELL_PRINTF("Error: process %d is not stopped (state: %s)\n",
                   pid, tiku_process_state_str(p->state));
        return;
    }

    if (tiku_process_resume(pid) == 0) {
        SHELL_PRINTF("Resumed process %d (%s)\n", pid,
                   p->name ? p->name : "(null)");
    } else {
        SHELL_PRINTF("Error: failed to resume process %d\n", pid);
    }
}
