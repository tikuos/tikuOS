/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.c - "basic" command: REPL, run/resume, /data files.
 *
 * Dispatches to the interpreter under basic/ (tiku_basic.h) and
 * moves program text between the interpreter's store and /data files.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_basic.h"
#include <basic/tiku_basic.h>
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* FILE BRIDGE — load / save / run a program stored as a /data file          */
/*---------------------------------------------------------------------------*/

/* Scratch for file<->program transfers.  Static (the shell runs commands one
 * at a time).  It bounds both directions: a program file or saved program
 * longer than TIKU_BASIC_FILE_MAX - 1 bytes is refused. */
#ifndef TIKU_BASIC_FILE_MAX
#define TIKU_BASIC_FILE_MAX  600u
#endif
static char basic_file_buf[TIKU_BASIC_FILE_MAX];

/** @brief Load @p path's program; @p run also executes it (implicit RUN). */
static void
basic_from_file(const char *path, int run)
{
    char   resolved[TIKU_SHELL_CWD_SIZE];
    size_t total;
    int    n;

    /* Returns without output while a BASIC session is active, so a job or
     * rule that fires `basic run/load <path>` during a session (jobs tick
     * before the BASIC mode tick) neither replaces the session's program nor
     * starts a blocking run in the middle of it. */
    if (tiku_basic_mode_active()) {
        return;
    }
    tiku_shell_cwd_resolve(path, resolved, sizeof resolved);
    n = tiku_vfs_read_total(resolved, basic_file_buf, sizeof basic_file_buf,
                            &total);
    if (n < 0) {
        SHELL_PRINTF("basic: cannot read '%s'\n", resolved);
        return;
    }
    /* A file that does not fit with its NUL is refused whole. */
    if (total >= sizeof basic_file_buf) {
        SHELL_PRINTF("basic: '%s' is longer than %u bytes\n", resolved,
                     (unsigned)(sizeof basic_file_buf - 1u));
        return;
    }
    basic_file_buf[n] = '\0';

    /* Load into the program store (the same path /data/basic uses); for `run`,
     * autorun then reloads the program from that store and runs it. */
    if (tiku_basic_vfs_write(basic_file_buf, (unsigned int)n) != 0) {
        SHELL_PRINTF("basic: load failed\n");
        return;
    }
    if (run) {
        tiku_basic_autorun();
    }
}

/** @brief Save the current program text to @p path. */
static void
basic_to_file(const char *path)
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    int  n;

    tiku_shell_cwd_resolve(path, resolved, sizeof resolved);
    /* Bounded as a load is, so a saved file always loads back.  The read
     * gives 0 both for no program and for one too long. */
    n = tiku_basic_vfs_read(basic_file_buf, sizeof basic_file_buf - 1u);
    if (n <= 0) {
        SHELL_PRINTF("basic: no program to save, or longer than %u bytes\n",
                     (unsigned)(sizeof basic_file_buf - 1u));
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

    /* `basic resume` / `basic run resume`: continue the saved program from its
     * checkpoint, or start it fresh if there is none. */
    if (sub != NULL && strcmp(sub, "resume") == 0) {
        (void)tiku_basic_mode_resume_saved();
        return;
    }
    if (sub != NULL && strcmp(sub, "run") == 0) {
        if (argc >= 3u && strcmp(argv[2], "resume") == 0) {
            (void)tiku_basic_mode_resume_saved();
        } else if (argc >= 3u) {
            basic_from_file(argv[2], 1);       /* run <path> (blocking) */
        } else {
            (void)tiku_basic_mode_run_saved(); /* run saved, non-blocking */
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
    tiku_basic_mode_enter();
}
