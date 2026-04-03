/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_start.c - "start" command implementation
 *
 * Starts or resumes a process by name.  Searches the active process
 * registry first (resume if stopped), then the process catalog
 * (register + start if available but not yet running).
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

#include "tiku_shell_cmd_start.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/process/tiku_process.h>

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: list (no arguments)                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief List all catalog entries with their current status.
 */
static void
cmd_start_list(void)
{
    uint8_t count = tiku_process_catalog_count();
    uint8_t i;

    if (count == 0) {
        SHELL_PRINTF("(no processes in catalog)\n");
        return;
    }

    SHELL_PRINTF("Available processes:\n");
    for (i = 0; i < count; i++) {
        const tiku_process_catalog_entry_t *e;
        struct tiku_process *active;
        const char *status;

        e = tiku_process_catalog_get(i);
        if (e == NULL) {
            continue;
        }

        /* Check if this process is already in the active registry */
        active = tiku_process_find_by_name(e->name);
        if (active != NULL) {
            status = tiku_process_state_str(active->state);
        } else {
            status = "available";
        }

        SHELL_PRINTF("  %-12s  %s\n", e->name, status);
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_start(uint8_t argc, const char *argv[])
{
    const char *name;
    struct tiku_process *p;

    if (argc < 2) {
        cmd_start_list();
        return;
    }

    name = argv[1];

    /* 1. Check the active registry — process already registered? */
    p = tiku_process_find_by_name(name);
    if (p != NULL) {
        if (p->state == TIKU_PROCESS_STATE_STOPPED) {
            if (tiku_process_resume(p->pid) == 0) {
                SHELL_PRINTF("Resumed '%s' (pid %d)\n", name, p->pid);
            } else {
                SHELL_PRINTF("Error: could not resume '%s'\n", name);
            }
        } else {
            SHELL_PRINTF("'%s' already running (pid %d)\n", name, p->pid);
        }
        return;
    }

    /* 2. Check the catalog — available but not yet started? */
    p = tiku_process_catalog_find(name);
    if (p != NULL) {
        /* Use p->name (stable string literal from TIKU_PROCESS)
         * rather than argv name (ephemeral parser scratch buffer) */
        int8_t pid = tiku_process_register(p->name, p);
        if (pid >= 0) {
            SHELL_PRINTF("Started '%s' (pid %d)\n", name, pid);
        } else {
            SHELL_PRINTF("Error: process registry full\n");
        }
        return;
    }

    /* 3. Not found anywhere */
    SHELL_PRINTF("Unknown process '%s'\n", name);
}
