/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ip.c - "ip" command: print the device's IPv4 address.
 *
 * Reads the address from the IPv4 layer and prints it as dotted-quad, with
 * the DHCP lease when there is one.  It is reachable at once on WiFi, or once
 * SLIP carries the wire.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_ip.h"
#include <kernel/shell/tiku_shell.h>              /* SHELL_PRINTF, cmd flags */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h> /* ipv4_get_addr */
#if TIKU_SHELL_CMD_SLIP
#include "tiku_shell_cmd_slip.h"                   /* slip_active */
#endif
#if defined(TIKU_KITS_NET_WIFI_ENABLE)
#include <tikukits/net/wifi/tiku_kits_net_wifi.h>  /* the WiFi link */
#endif
#if defined(TIKU_KITS_NET_DHCP_ENABLE) && TIKU_KITS_NET_DHCP_ENABLE
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>  /* the lease */
#endif

/** @brief "label a.b.c.d" for one address of the lease. */
static void
ip_put(const char *label, const uint8_t a[4])
{
    SHELL_PRINTF("%s%u.%u.%u.%u\n", label, a[0], a[1], a[2], a[3]);
}

void
tiku_shell_cmd_ip(uint8_t argc, const char *argv[])
{
    const uint8_t *a = tiku_kits_net_ipv4_get_addr();

    (void)argc;
    (void)argv;

    /* Print the address and any DHCP lease, then whether the host can reach
     * it: at once on WiFi, over SLIP only once SLIP is on (matching the
     * host-side SLIP indicator). */
    SHELL_PRINTF("IPv4: %u.%u.%u.%u\n", a[0], a[1], a[2], a[3]);
#if defined(TIKU_KITS_NET_DHCP_ENABLE) && TIKU_KITS_NET_DHCP_ENABLE
    {
        const tiku_kits_net_dhcp_lease_t *l = tiku_kits_net_dhcp_get_lease();

        if (l != (const tiku_kits_net_dhcp_lease_t *)0) {
            ip_put("Mask: ", l->mask);
            ip_put("Gateway: ", l->gateway);
            ip_put("DNS: ", l->dns);
            SHELL_PRINTF("Lease: %lu s\n", (unsigned long)l->lease_sec);
        }
    }
#endif
#if defined(TIKU_KITS_NET_WIFI_ENABLE)
    if (tiku_kits_net_ipv4_get_link() == &tiku_kits_net_wifi_link) {
        SHELL_PRINTF("reachable now -- on WiFi\n");
        return;
    }
#endif
#if TIKU_SHELL_CMD_SLIP
    if (tiku_shell_cmd_slip_active())
        SHELL_PRINTF("reachable now -- SLIP is on\n");
    else
        SHELL_PRINTF("not reachable yet -- run 'slip' to put it on the wire\n");
#endif
}
