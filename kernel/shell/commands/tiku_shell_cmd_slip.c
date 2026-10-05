/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_slip.c - "slip" command: toggle SLIP/IP on the console line.
 *
 * Registers the IPv4 channel on the console: a frame whose first byte has the
 * IPv4 version nibble (0x4N) goes whole to the IP stack, and text between
 * frames still reaches the shell's line editor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_slip.h"
#include <kernel/shell/tiku_shell.h>                 /* SHELL_PRINTF */
#include <tikukits/net/tiku_kits_net.h>              /* TIKU_KITS_NET_MTU */
#include <tikukits/net/slip/tiku_kits_net_slip.h>    /* slip_init, slip_link */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>    /* get/set_link, input */
#include <kernel/console/tiku_console.h>

static uint8_t slip_on;       /* the console's IPv4 channel is registered */
static uint8_t link_ready;    /* 1 after the first enable: an IP link is set */
static uint8_t slip_frame_buf[TIKU_KITS_NET_MTU];

/** @brief One IP packet from the console's channel, into the stack. */
static void
slip_frame(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    tiku_kits_net_ipv4_input(buf, (uint16_t)len);
}

uint8_t
tiku_shell_cmd_slip_active(void)
{
    return slip_on;
}

void
tiku_shell_cmd_slip_enable(void)
{
    if (!link_ready) {
        /* Install SLIP as the IP link only when no link is set.  On a WiFi
         * board `wifi up` installs the WiFi link first, and it must stay:
         * ping, ntp and dns call this function there too, and their
         * replies arrive from the radio callback. */
        if (tiku_kits_net_ipv4_get_link() == (const tiku_kits_net_link_t *)0) {
            tiku_kits_net_slip_init();
            tiku_kits_net_ipv4_set_link(&tiku_kits_net_slip_link);
            /* The address is left as it is: the build-time default, or one
             * set through /sys/net/ipv4/address before SLIP was enabled. */
        }
        link_ready = 1;
    }
    (void)tiku_console_add_channel(0x40u, 0xF0u, 1u, slip_frame, (void *)0,
                                   slip_frame_buf, sizeof slip_frame_buf);
    slip_on = 1;
}

void
tiku_shell_cmd_slip(uint8_t argc, const char *argv[])
{
    uint8_t want;

    if (argc >= 2 && argv[1][0] == 'o' && argv[1][1] == 'n') {
        want = 1u;                          /* "slip on"  -> enable */
    } else if (argc >= 2 && argv[1][0] == 'o' && argv[1][1] == 'f') {
        want = 0u;                          /* "slip off" -> disable */
    } else {
        want = slip_on ? 0u : 1u;           /* "slip"     -> toggle */
    }

    if (want) {
        tiku_shell_cmd_slip_enable();
        SHELL_PRINTF("SLIP on. The console line carries SLIP/IP and text;"
                     " drive it with the slmux host tool ('ping <ip>' works"
                     " too).\n");
    } else {
        tiku_console_remove_channel(0x40u, 0xF0u);
        slip_on = 0;
        SHELL_PRINTF("SLIP off -- text only on the console line.\n");
    }
}
