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
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/* Largest node value `read`/`cat` prints in one shot.  The old 64-byte buffer
 * silently truncated /data files at 63 bytes (a newcomer's first surprise);
 * size it to a full file-store slot so whole files display.  The buffer is
 * static, not on the (small) shell-task stack, so the larger size is safe.
 * Override TIKU_SHELL_READ_MAX in the build to trade RAM for capacity. */
#ifndef TIKU_SHELL_READ_MAX
#  if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350)
#    define TIKU_SHELL_READ_MAX 8192    /* holds a whole file-store slot (4 KB)
                                         * plus headroom for the /sys/vfs/manifest
                                         * dump; these parts have RAM to spare  */
#  else
#    define TIKU_SHELL_READ_MAX 512     /* = one MSP430 FRAM slot            */
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_read(uint8_t argc, const char *argv[])
{
    char         resolved[TIKU_SHELL_CWD_SIZE];
    static char  buf[TIKU_SHELL_READ_MAX + 1];   /* +1 for the NUL terminator */
    int          n;

    if (argc < 2) {
        SHELL_PRINTF("Usage: read <path>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    n = tiku_vfs_read(resolved, buf, sizeof(buf) - 1);
    if (n < 0) {
        /* Keep the "cannot read" phrasing (host tooling matches it) and append
         * the machine-readable status so an agent can tell ENOENT from EACCES. */
        SHELL_PRINTF("read: cannot read '%s' (%s)\n", resolved,
                     tiku_vfs_strerror(n));
        return;
    }

    buf[n] = '\0';
    SHELL_PRINTF("%s", buf);

    /* Finish on a newline so the next prompt starts on its own line.  Files
     * often have no trailing newline and /sys values never do, so without this
     * the value glues to the prompt ("aatikuOS:/>").  Skip it when the content
     * already ends in '\n' to avoid a blank line. */
    if (n == 0 || buf[n - 1] != '\n') {
        SHELL_PRINTF("\n");
    }
}
