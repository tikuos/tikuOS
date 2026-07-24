/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_slip.c - "slip" command: toggle SLIP/IP networking on the
 *                         shared console UART
 *
 * Net is a compile-time choice (TIKU_KIT_NET_ENABLE=1).  `slip` toggles a
 * runtime mode in which the shell's RX loop demultiplexes the single UART:
 * SLIP frames (0xC0-delimited) are routed to the IP stack while ordinary
 * keystrokes still reach the line editor, so the shell stays interactive AND
 * the device is an IP node at the same time (use the slmux host tool to drive
 * both over one wire).  Running `slip` again turns it back to console-only --
 * no reset needed.  `ping` enables this mode automatically.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_slip.h"
#include <kernel/shell/tiku_shell.h>                 /* SHELL_PRINTF */
#include <tikukits/net/tiku_kits_net.h>              /* TIKU_KITS_NET_IP_ADDR */
#include <tikukits/net/slip/tiku_kits_net_slip.h>    /* slip_init, slip_link */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>    /* set_link, set_addr */

static uint8_t slip_on;       /* demux active: UART carries SLIP/IP + console */
static uint8_t link_ready;    /* SLIP link registered with the IP layer once */

uint8_t
tiku_shell_cmd_slip_active(void)
{
    return slip_on;
}

void
tiku_shell_cmd_slip_enable(void)
{
    if (!link_ready) {
        /* Only claim the IP link for SLIP when nothing else already owns it.
         * On a WiFi board, `wifi up` installs the WiFi link backend and a
         * DHCP-acquired address first; forcing SLIP here would drop the radio
         * link and reset the address to the compile-time default -- breaking
         * net client commands (ping/ntp/dns) that call this to ensure the RX
         * path is live.  Over WiFi those commands need nothing from us: the
         * link is up and RX is pushed from the radio callback.  With no link
         * set (the usual SLIP-over-UART build) we install SLIP as before. */
        if (tiku_kits_net_ipv4_get_link() == (const tiku_kits_net_link_t *)0) {
            static const uint8_t self[4] = TIKU_KITS_NET_IP_ADDR;
            tiku_kits_net_slip_init();
            tiku_kits_net_ipv4_set_link(&tiku_kits_net_slip_link);
            tiku_kits_net_ipv4_set_addr(self);
        }
        link_ready = 1;
    }
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
        SHELL_PRINTF("SLIP on. UART carries SLIP/IP + console; drive it with"
                     " the slmux host tool ('ping <ip>' works too).\n");
    } else {
        slip_on = 0;
        SHELL_PRINTF("SLIP off -- console-only on the UART.\n");
    }
}
