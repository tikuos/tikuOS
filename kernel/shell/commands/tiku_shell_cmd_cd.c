/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cd.c - "cd" and "pwd" command implementations
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

#include "tiku_shell_cmd_cd.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <server/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLERS                                                           */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_cd(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    const char *target;

    if (argc < 2) {
        tiku_shell_cwd_set("/");
        return;
    }

    target = argv[1];
    tiku_shell_cwd_resolve(target, resolved, sizeof(resolved));

    /* Verify the target is a valid VFS directory */
    {
        const tiku_vfs_node_t *n = tiku_vfs_resolve(resolved);
        if (n == (const tiku_vfs_node_t *)0) {
            SHELL_PRINTF("cd: no such directory '%s'\n", target);
            return;
        }
        if (n->type != TIKU_VFS_DIR) {
            SHELL_PRINTF("cd: not a directory '%s'\n", target);
            return;
        }
    }

    tiku_shell_cwd_set(resolved);
}

void
tiku_shell_cmd_pwd(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    SHELL_PRINTF("%s\n", tiku_shell_cwd_get());
}
