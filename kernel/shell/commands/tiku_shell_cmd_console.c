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
#include <kernel/link/tiku_link_console.h>

/* An echo link on a spare channel, armed on demand: it sends every message
 * it receives straight back, so a host can prove the link end to end over
 * the port without a desktop.  Off by default; it costs a channel slot. */
#define ECHO_MARKER 0xF2u
static tiku_link_console_t echo_state;
static tiku_link_t *echo_link;
static uint8_t echo_buf[256];

/** @brief Send a received message straight back. */
static void
echo_recv(void *ctx, uint8_t *msg, size_t len)
{
    (void)ctx;
    (void)tiku_link_send(echo_link, msg, len);
}

/** @brief Arm or disarm the echo link.  @return the new state. */
static uint8_t
echo(uint8_t on)
{
    if (on && echo_link == (tiku_link_t *)0) {
        echo_link = tiku_link_console_open(&echo_state, ECHO_MARKER,
                                           echo_buf, sizeof echo_buf);
        if (echo_link != (tiku_link_t *)0) {
            tiku_link_on_recv(echo_link, echo_recv, (void *)0);
        }
    } else if (!on && echo_link != (tiku_link_t *)0) {
        tiku_link_close(echo_link);
        echo_link = (tiku_link_t *)0;
    }
    return echo_link != (tiku_link_t *)0;
}

void
tiku_shell_cmd_console(uint8_t argc, const char *argv[])
{
    const tiku_console_stats_t *st = tiku_console_stats();
    uint8_t slot;

    if (argc >= 2 && argv[1][0] == 'e') {          /* console echo [on|off] */
        uint8_t want = !(argc >= 3 && argv[2][0] == 'o' && argv[2][1] == 'f');

        SHELL_PRINTF("echo link on 0x%02X: %s\n", ECHO_MARKER,
                     echo(want) ? "on" : "off");
        return;
    }
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
