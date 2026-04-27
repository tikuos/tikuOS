/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.c - "watch" command implementation
 *
 * Periodically reads a VFS node and prints its value, providing a
 * lightweight "live" view of any kernel-exposed state (sensors, power
 * rails, GPIO, /proc nodes).  Runs synchronously inside the shell
 * protothread; cancellation is detected by polling the active I/O
 * backend for Ctrl+C (0x03) during the inter-read wait window.
 *
 * Design notes:
 *  - The wait is decomposed into 1-second chunks so the deadline
 *    arithmetic stays well below the 16-bit clock-tick wraparound,
 *    independent of TIKU_CLOCK_SECOND.
 *  - Trailing whitespace from the VFS read is stripped so each
 *    iteration produces exactly one indented line of output.
 *  - The command does not print a prompt; the shell main loop does
 *    that after the handler returns.
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

#include "tiku_shell_cmd_watch.h"
#include <kernel/shell/tiku_shell.h>      /* SHELL_PRINTF, I/O backend */
#include <kernel/shell/tiku_shell_cwd.h>  /* tiku_shell_cwd_resolve */
#include <kernel/vfs/tiku_vfs.h>          /* tiku_vfs_read */
#include <kernel/timers/tiku_clock.h>     /* tiku_clock_time, TIKU_CLOCK_LT */

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Ctrl+C / ETX — terminates the watch loop */
#define TIKU_SHELL_WATCH_CANCEL  0x03

/** Maximum supported interval (seconds).  Bounds the 1-second chunk
 *  loop and keeps argv parsing in uint8_t range. */
#define TIKU_SHELL_WATCH_MAX_SEC 255

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Parse a small unsigned decimal (1..TIKU_SHELL_WATCH_MAX_SEC).
 * @return 1 on success (value written to *out), 0 on parse error.
 */
static uint8_t
watch_parse_interval(const char *s, uint8_t *out)
{
    uint16_t val = 0;
    uint8_t i;

    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        val = val * 10 + (uint16_t)(s[i] - '0');
        if (val > TIKU_SHELL_WATCH_MAX_SEC) {
            return 0;
        }
    }
    if (val == 0) {
        return 0;
    }
    *out = (uint8_t)val;
    return 1;
}

/**
 * @brief Wait for one second, returning early if Ctrl+C arrives.
 * @return 1 if cancelled, 0 if the second elapsed normally.
 */
static uint8_t
watch_wait_one_second(void)
{
    tiku_clock_time_t deadline = tiku_clock_time() + TIKU_CLOCK_SECOND;

    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == TIKU_SHELL_WATCH_CANCEL) {
                return 1;
            }
            /* Other keystrokes are discarded while watch is running. */
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_watch(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char buf[64];
    uint8_t interval_sec = 1;
    uint8_t s;
    int n;

    if (argc < 2 || argc > 3) {
        SHELL_PRINTF("Usage: watch <path> [interval]\n");
        return;
    }

    if (argc == 3 && !watch_parse_interval(argv[2], &interval_sec)) {
        SHELL_PRINTF("watch: interval must be 1..%u\n",
                     (unsigned)TIKU_SHELL_WATCH_MAX_SEC);
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    while (1) {
        n = tiku_vfs_read(resolved, buf, sizeof(buf) - 1);
        if (n < 0) {
            SHELL_PRINTF("watch: cannot read '%s'\n", resolved);
            return;
        }
        buf[n] = '\0';

        /* Strip trailing CR/LF/space so each line is uniform */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'
                         || buf[n - 1] == ' ')) {
            buf[--n] = '\0';
        }

        SHELL_PRINTF("  %s\n", buf);

        for (s = 0; s < interval_sec; s++) {
            if (watch_wait_one_second()) {
                SHELL_PRINTF("^C\n");
                return;
            }
        }
    }
}
