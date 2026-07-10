/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ip.c - "ip" command: print the device's IPv4 address
 *
 * Reads the current address straight from the IPv4 layer
 * (tiku_kits_net_ipv4_get_addr(), initialised at compile time from
 * TIKU_KITS_NET_IP_ADDR and updated by DHCP if used) and prints it as
 * dotted-quad.  The address is reachable from the host once SLIP carries
 * the wire -- run `slip` (or `ping`, which turns it on).
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
