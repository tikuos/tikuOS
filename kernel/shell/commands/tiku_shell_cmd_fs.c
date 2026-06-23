/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fs.c - "rm" / "touch" file commands for the /data store
 *
 * Thin wrappers over the VFS: the file store mounts /data as a dynamic
 * directory, so these operate on any dynamic child by path.  "write" creates
 * and overwrites; these add removal and no-truncate creation.
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

#include "tiku_shell_cmd_fs.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLERS                                                           */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_rm(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];

    if (argc < 2u) {
        SHELL_PRINTF("Usage: rm <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    if (tiku_vfs_unlink(resolved) < 0) {
        SHELL_PRINTF("rm: cannot remove '%s'\n", resolved);
    }
}

void
tiku_shell_cmd_touch(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char probe[1];

    if (argc < 2u) {
        SHELL_PRINTF("Usage: touch <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Already exists -> no-op (no mtime to bump), so we never truncate it. */
    if (tiku_vfs_read(resolved, probe, sizeof(probe)) >= 0) {
        return;
    }
    if (tiku_vfs_write(resolved, "", 0) < 0) {
        SHELL_PRINTF("touch: cannot create '%s'\n", resolved);
    }
}
