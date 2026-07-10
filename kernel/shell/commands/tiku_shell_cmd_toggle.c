/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_toggle.c - "toggle" command implementation
 *
 * Writes "t" (toggle) to a writable VFS node, then reads back and
 * prints the new state.  Generic — works with any node that
 * interprets 't' as a binary flip (LEDs, GPIOs, flags, etc.).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_toggle.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_toggle(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char readbuf[16];
    int n;

    if (argc < 2) {
        SHELL_PRINTF("Usage: toggle <path>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    if (tiku_vfs_write(resolved, "t", 1) < 0) {
        SHELL_PRINTF("toggle: cannot write '%s'\n", resolved);
        return;
    }

    /* Read back and show new state */
    n = tiku_vfs_read(resolved, readbuf, sizeof(readbuf) - 1);
    if (n > 0) {
        readbuf[n] = '\0';
        SHELL_PRINTF("%s: %s", resolved, readbuf);
    }
}
