/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_changed.c - "changed" command implementation
 *
 * Synchronous blocking equivalent of an "on changed" rule, useful at
 * the prompt for one-off "wait for next event" debugging.  Polls the
 * VFS path at shell-tick granularity (~50 ms) so the latency-to-
 * detection is bounded by one tick; cancellation via Ctrl+C is
 * detected on every loop iteration and so is sub-tick.
 *
 * Trailing whitespace is stripped from both the baseline and current
 * reads so a path that returns "23.4\n" reads as "23.4" and does not
 * spuriously appear "changed" simply due to a trailing newline.
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

/** Per-iteration buffer size for VFS reads.  32 bytes covers all
 *  realistic numeric or short-string VFS values. */
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
 * @brief Wait one shell-tick worth of time, polling for Ctrl+C.
 * @return 1 if cancelled, 0 if the interval elapsed normally.
 */
static uint8_t
changed_wait_tick(void)
{
    /* TIKU_CLOCK_SECOND/20 == ~50 ms at the default 128 Hz tick.
     * Matches the shell's own poll cadence so detection latency
     * tracks the rest of the system. */
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
    prev[prev_n] = '\0';
    prev_n = changed_rtrim(prev, prev_n);

    while (1) {
        if (changed_wait_tick()) {
            SHELL_PRINTF("^C\n");
            return;
        }

        curr_n = tiku_vfs_read(resolved, curr, sizeof(curr) - 1);
        if (curr_n < 0) {
            /* Transient read failure: keep waiting. */
            continue;
        }
        curr[curr_n] = '\0';
        curr_n = changed_rtrim(curr, curr_n);

        if (curr_n != prev_n || strcmp(prev, curr) != 0) {
            SHELL_PRINTF("  %s -> %s\n", prev, curr);
            return;
        }
    }
}
