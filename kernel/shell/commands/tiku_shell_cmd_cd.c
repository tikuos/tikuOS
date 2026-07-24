/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cd.c - "cd" and "pwd" command implementations
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_cd.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

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

    /* Verify the target is a directory -- static, or a virtual sub-folder of a
     * dynamic store (e.g. /data/logs), which does not resolve to a node. */
    if (!tiku_vfs_is_dir(resolved)) {
        SHELL_PRINTF("cd: no such directory '%s'\n", target);
        return;
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
