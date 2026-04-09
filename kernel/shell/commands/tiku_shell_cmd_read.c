/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_read.c - "read" command implementation
 *
 * Reads a VFS node and prints its value.  Works with any readable
 * node: /sys/uptime, /sys/mem/sram, /dev/led0, etc.
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

#include "tiku_shell_cmd_read.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_read(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char buf[64];
    int n;

    if (argc < 2) {
        SHELL_PRINTF("Usage: read <path>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    n = tiku_vfs_read(resolved, buf, sizeof(buf) - 1);
    if (n < 0) {
        SHELL_PRINTF("read: cannot read '%s'\n", resolved);
        return;
    }

    buf[n] = '\0';
    SHELL_PRINTF("%s", buf);
}
