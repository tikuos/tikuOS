/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_write.c - "write" command implementation
 *
 * Writes argv[2] to a VFS node through its write handler.  Success
 * prints nothing; a failure prints the VFS status.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_write.h"
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
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
    int rc;

    if (argc < 3) {
        SHELL_PRINTF("Usage: write <path> <value>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    value = argv[2];
    len = (uint8_t)strlen(value);

    rc = tiku_vfs_write(resolved, value, len);
    if (rc < 0) {
        /* Host tooling matches "cannot write"; the status name follows. */
        SHELL_PRINTF("write: cannot write '%s' (%s)\n", resolved,
                     tiku_vfs_strerror(rc));
    }
}
