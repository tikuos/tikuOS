/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_write.c - "write" command implementation
 *
 * Writes a value string to a writable VFS node.  More general than
 * "toggle" — supports any value the node's write handler accepts.
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

#include "tiku_shell_cmd_write.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <server/vfs/tiku_vfs.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_write(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    const char *value;
    uint8_t len;

    if (argc < 3) {
        SHELL_PRINTF("Usage: write <path> <value>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    value = argv[2];
    len = (uint8_t)strlen(value);

    if (tiku_vfs_write(resolved, value, len) < 0) {
        SHELL_PRINTF("write: cannot write '%s'\n", resolved);
    }
}
