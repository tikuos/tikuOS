/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.c - "basic" shell command stub.
 *
 * Thin dispatch wrapper that the shell command table calls.  The
 * actual interpreter engine lives at kernel/shell/basic/ and is
 * exposed via tiku_basic.h:
 *
 *   `basic`            -> tiku_basic_repl()    (interactive REPL)
 *   `basic run`        -> tiku_basic_autorun() (run the saved program)
 *   `basic run  <path>`-> load a /data file and run it
 *   `basic load <path>`-> load a /data file into the program store
 *   `basic save <path>`-> save the current program to a /data file
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_basic.h"
#include <kernel/shell/basic/tiku_basic.h>
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* FILE BRIDGE — load / save / run a program stored as a /data file          */
/*---------------------------------------------------------------------------*/

/* Scratch for file<->program transfers.  Static (the shell runs commands one
 * at a time) and sized past the largest file slot so a whole program fits. */
#ifndef TIKU_BASIC_FILE_MAX
#define TIKU_BASIC_FILE_MAX  600u
#endif
static char basic_file_buf[TIKU_BASIC_FILE_MAX];

/* Load @p path's program text; @p run also executes it (implicit RUN). */
static void
basic_from_file(const char *path, int run)
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    int  n;

    tiku_shell_cwd_resolve(path, resolved, sizeof resolved);
    n = tiku_vfs_read(resolved, basic_file_buf, sizeof basic_file_buf - 1u);
    if (n < 0) {
        SHELL_PRINTF("basic: cannot read '%s'\n", resolved);
        return;
    }
    basic_file_buf[n] = '\0';

    /* Load into the program store (the same path /data/basic uses), then -- for
     * `run` -- execute it via autorun, which reloads from that store and runs.
     * This reuses the proven load+autorun path. */
    if (tiku_basic_vfs_write(basic_file_buf, (unsigned int)n) != 0) {
        SHELL_PRINTF("basic: load failed\n");
        return;
    }
    if (run) {
        tiku_basic_autorun();
    }
}

/* Save the current program text to @p path. */
static void
basic_to_file(const char *path)
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    int  n;

    tiku_shell_cwd_resolve(path, resolved, sizeof resolved);
    n = tiku_basic_vfs_read(basic_file_buf, sizeof basic_file_buf);
    if (n <= 0) {
        SHELL_PRINTF("basic: no program to save\n");
        return;
    }
    if (tiku_vfs_write(resolved, basic_file_buf, (size_t)n) < 0) {
        SHELL_PRINTF("basic: cannot write '%s'\n", resolved);
    }
}

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_basic(uint8_t argc, const char *argv[])
{
    const char *sub = (argc >= 2u) ? argv[1] : NULL;

    if (sub != NULL && strcmp(sub, "run") == 0) {
        if (argc >= 3u) {
            basic_from_file(argv[2], 1);   /* run <path> */
        } else {
            tiku_basic_autorun();          /* run saved  */
        }
        return;
    }
    if (sub != NULL && argc >= 3u && strcmp(sub, "load") == 0) {
        basic_from_file(argv[2], 0);
        return;
    }
    if (sub != NULL && argc >= 3u && strcmp(sub, "save") == 0) {
        basic_to_file(argv[2]);
        return;
    }
    tiku_basic_repl();
}
