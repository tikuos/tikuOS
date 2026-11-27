/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mailbox.c - "mailbox" command implementation.
 *
 * The files themselves are under /mailbox; this command formats the part,
 * checks it and runs a write-and-read-back loop over the shared bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_mailbox.h"
#include <shell/tiku_shell.h>
#include <services/mailbox/tiku_mailbox_vfs.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Largest file the stress loop writes: its two buffers are static. */
#ifdef PLATFORM_MSP430
#define MB_STRESS_MAX  64u
#else
#define MB_STRESS_MAX  256u
#endif

static uint8_t stress_out[MB_STRESS_MAX], stress_in[MB_STRESS_MAX];

/** @brief Print a failed call's status. */
static void
mb_fail(const char *what, int rc)
{
    SHELL_PRINTF("mailbox: %s failed (%s)\n", what, tiku_vfs_strerror(rc));
}

/** @brief Check the files and print what the mailbox holds. */
static void
mb_show(void)
{
    tiku_mailbox_stats_t st;
    int rc = tiku_mailbox_check(&tiku_mailbox_board, &st);

    if (rc != 0) {
        mb_fail("check", rc);
        return;
    }
    SHELL_PRINTF("files %u (kept %u, read %u)  used %lu  free %lu  "
                 "largest %lu\n", (unsigned)st.files, (unsigned)st.kept,
                 (unsigned)st.read, (unsigned long)st.used,
                 (unsigned long)st.free, (unsigned long)st.largest);
    SHELL_PRINTF("bad %u  reclaimed %lu blocks\n", (unsigned)st.bad,
                 (unsigned long)st.reclaimed);
}

/** @brief Write a file @p count times, reading each copy back. */
static void
mb_stress(unsigned long count, unsigned long bytes, const char *name)
{
    tiku_clock_time_t t0 = tiku_clock_time();
    unsigned long i, bad = 0UL;
    uint32_t j;

    if (bytes == 0UL || bytes > MB_STRESS_MAX) {
        SHELL_PRINTF("mailbox: bytes 1 to %u\n", (unsigned)MB_STRESS_MAX);
        return;
    }
    for (i = 0UL; i < count; i++) {
        uint32_t total = 0UL;
        int rc;

        for (j = 0UL; j < bytes; j++) {
            stress_out[j] = (uint8_t)(i * 13UL + j * 7UL);
        }
        rc = tiku_mailbox_write(&tiku_mailbox_board, name, stress_out,
                                (uint32_t)bytes);
        if (rc == 0) {
            rc = tiku_mailbox_read_at(&tiku_mailbox_board, name, 0UL,
                                      stress_in, (uint32_t)bytes, &total);
        }
        if (rc != (int)bytes || total != bytes ||
            memcmp(stress_in, stress_out, (size_t)bytes) != 0) {
            if (bad == 0UL) {
                SHELL_PRINTF("mailbox: copy %lu failed (%d)\n", i, rc);
            }
            bad++;
        }
        tiku_watchdog_kick();
    }
    {
        char out[TIKU_MAILBOX_NAME_MAX], line[48];
        unsigned long ticks = (unsigned long)(tiku_clock_time_t)
                              (tiku_clock_time() - t0);
        int n = snprintf(line, sizeof(line), "%lu of %lu whole, %lu ticks\n",
                         count - bad, count, ticks);

        SHELL_PRINTF("stress: %s", line);
        /* The result also goes in the mailbox, for the other processor. */
        if (strlen(name) + 5u <= sizeof(out) && n > 0) {
            strcpy(out, name);
            strcat(out, ".out");
            (void)tiku_mailbox_write(&tiku_mailbox_board, out,
                                     (const uint8_t *)line, (uint32_t)n);
        }
    }
}

void
tiku_shell_cmd_mailbox(uint8_t argc, const char *argv[])
{
    int rc;

    if (argc < 2) {
        mb_show();
        return;
    }
    if (strcmp(argv[1], "format") == 0) {
        rc = tiku_mailbox_format(&tiku_mailbox_board);
        if (rc != 0) {
            mb_fail("format", rc);
        } else {
            mb_show();
        }
        return;
    }
    if (strcmp(argv[1], "stress") == 0 && argc >= 3) {
        mb_stress(strtoul(argv[2], (char **)0, 10),
                  (argc >= 4) ? strtoul(argv[3], (char **)0, 10) : 32UL,
                  (argc >= 5) ? argv[4] : "stress");
        return;
    }
    SHELL_PRINTF("Usage: mailbox [format | stress <count> [bytes] [name]]\n");
}
