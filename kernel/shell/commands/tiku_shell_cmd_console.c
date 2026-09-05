/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_console.c - "console" command: the line's channels and
 * counters, as one line per channel and one of totals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_console.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/console/tiku_console.h>

void
tiku_shell_cmd_console(uint8_t argc, const char *argv[])
{
    const tiku_console_stats_t *st = tiku_console_stats();
    uint8_t slot;

    (void)argc;
    (void)argv;
    for (slot = 0; slot < TIKU_CONSOLE_CHANNELS; slot++) {
        uint8_t value, mask;
        size_t cap;

        if (tiku_console_channel_at(slot, &value, &mask, &cap) == 0) {
            SHELL_PRINTF("channel 0x%02X mask=0x%02X cap=%u frames=%lu\n",
                         value, mask, (unsigned)cap,
                         (unsigned long)st->frames[slot]);
        }
    }
    SHELL_PRINTF("stray=%lu oversize=%lu phantom=%lu\n",
                 (unsigned long)st->stray_end,
                 (unsigned long)st->oversize,
                 (unsigned long)st->phantom);
}
