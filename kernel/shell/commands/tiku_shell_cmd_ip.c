/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ip.c - "ip" command: print the device's IPv4 address.
 *
 * Reads the address from the IPv4 layer and prints it as dotted-quad.  It becomes
 * reachable from the host once SLIP carries the wire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_ip.h"
#include <kernel/shell/tiku_shell.h>                 /* SHELL_PRINTF, cmd flags */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>    /* ipv4_get_addr */
#if TIKU_SHELL_CMD_SLIP
#include "tiku_shell_cmd_slip.h"                      /* slip_active */
#endif

void
tiku_shell_cmd_ip(uint8_t argc, const char *argv[])
{
    const uint8_t *a = tiku_kits_net_ipv4_get_addr();

    (void)argc;
    (void)argv;

    /* The address is always configured (TIKU_KITS_NET_IP_ADDR), but it is only
     * reachable from the host once SLIP carries the wire -- say which, so this
     * matches the host-side SLIP indicator. */
    SHELL_PRINTF("IPv4: %u.%u.%u.%u\n", a[0], a[1], a[2], a[3]);
#if TIKU_SHELL_CMD_SLIP
    if (tiku_shell_cmd_slip_active())
        SHELL_PRINTF("reachable now -- SLIP is on\n");
    else
        SHELL_PRINTF("not reachable yet -- run 'slip' to put it on the wire\n");
#endif
}
