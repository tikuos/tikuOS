/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_changed.c - "changed" command implementation.
 *
 * Blocks the shell until a VFS value changes, re-reading it every
 * TIKU_CLOCK_SECOND / 20 ticks and checking for Ctrl+C on each pass.  Trailing
 * CR, LF and spaces are stripped before two values are compared.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_changed.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/timers/tiku_clock.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Ctrl+C / ETX — terminates the wait loop. */
#define CHANGED_CANCEL    0x03

/** Read buffer size; only the first CHANGED_BUF_SIZE - 1 bytes are compared. */
#define CHANGED_BUF_SIZE  32

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Strip trailing CR/LF/space in place.  Returns new length.
 */
static int
changed_rtrim(char *s, int n)
{
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                      s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
    return n;
}

/**
 * @brief Wait TIKU_CLOCK_SECOND / 20 ticks, polling for Ctrl+C.
 * @return 1 if cancelled, 0 if the interval elapsed normally.
 */
static uint8_t
changed_wait_tick(void)
{
    /* The same interval as TIKU_SHELL_POLL_TICKS, the shell's poll period. */
    tiku_clock_time_t deadline =
        tiku_clock_time() + (TIKU_CLOCK_SECOND / 20);

    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == CHANGED_CANCEL) {
                return 1;
            }
            /* Other keystrokes are discarded while waiting. */
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_changed(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char prev[CHANGED_BUF_SIZE];
    char curr[CHANGED_BUF_SIZE];
    int  prev_n;
    int  curr_n;

    if (argc != 2) {
        SHELL_PRINTF("Usage: changed <path>\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Establish the baseline; abort if the path is unreadable now. */
    prev_n = tiku_vfs_read(resolved, prev, sizeof(prev) - 1);
    if (prev_n < 0) {
        SHELL_PRINTF("changed: cannot read '%s'\n", resolved);
        return;
    }
    if (prev_n > (int)sizeof(prev) - 1) {   /* keep the NUL inside the buffer */
        prev_n = (int)sizeof(prev) - 1;
    }
    prev[prev_n] = '\0';
    prev_n = changed_rtrim(prev, prev_n);

    while (1) {
        if (changed_wait_tick()) {
            SHELL_PRINTF("^C\n");
            return;
        }

        curr_n = tiku_vfs_read(resolved, curr, sizeof(curr) - 1);
        if (curr_n < 0) {
            /* A failed read is skipped and the wait goes on. */
            continue;
        }
        if (curr_n > (int)sizeof(curr) - 1) {
            curr_n = (int)sizeof(curr) - 1;
        }
        curr[curr_n] = '\0';
        curr_n = changed_rtrim(curr, curr_n);

        if (curr_n != prev_n || strcmp(prev, curr) != 0) {
            SHELL_PRINTF("  %s -> %s\n", prev, curr);
            return;
        }
    }
}
