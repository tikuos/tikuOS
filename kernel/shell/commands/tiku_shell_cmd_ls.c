/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ls.c - "ls" command implementation
 *
 * Lists the children of a VFS directory node.  Directories are shown
 * with a trailing '/' to distinguish them from files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_ls.h"
#include <kernel/shell/tiku_shell.h>        /* SHELL_PRINTF */
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* LIST CALLBACK                                                             */
/*---------------------------------------------------------------------------*/

static void
ls_print_entry(const tiku_vfs_node_t *node, void *ctx)
{
    (void)ctx;

    if (node->type == TIKU_VFS_DIR) {
        SHELL_PRINTF("  d  %s/\n", node->name);
    } else {
        const char *perm = (node->read && node->write) ? "rw"
                         : node->read                  ? "r-"
                         : node->write                 ? "-w"
                         :                               "--";
        SHELL_PRINTF("  %s %s\n", perm, node->name);
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_ls(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];

    if (argc >= 2) {
        tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    } else {
        /* No argument — list the current directory */
        tiku_shell_cwd_resolve(".", resolved, sizeof(resolved));
    }

    if (tiku_vfs_list(resolved, ls_print_entry, (void *)0) < 0) {
        SHELL_PRINTF("ls: cannot list '%s'\n", resolved);
    }
}
