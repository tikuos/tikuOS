/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_dns.c - "dns" command implementation (async A-record lookup)
 *
 * Resolves a hostname to an IPv4 address over SLIP using the DNS stub
 * resolver.  Sends an A-record query to a recursive resolver (default
 * 8.8.8.8, reached through the SLIP host's relay/NAT) and prints the result.
 * The resolver is poll-based and counts each no-reply poll() as one retry
 * toward a 3-strike timeout, so this command paces polling at ~1 Hz (the
 * cadence the library documents) rather than once per shell tick.  I/O flows
 * through the shell's shared RX demux, so the shell stays interactive.
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

#include "tiku_shell_cmd_dns.h"
#include "tiku_shell_cmd_slip.h"                  /* tiku_shell_cmd_slip_enable */
#include <kernel/shell/tiku_shell.h>              /* SHELL_PRINTF */
#include <kernel/timers/tiku_clock.h>             /* tiku_clock_time */
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>  /* udp_init */
#include <tikukits/net/ipv4/tiku_kits_net_dns.h>  /* DNS stub resolver */

/*---------------------------------------------------------------------------*/
/* CONFIG + STATE                                                            */
/*---------------------------------------------------------------------------*/

/* Poll the (poll-based) resolver at the cadence it expects (~1 s); each
 * no-reply poll() counts as one retry toward its 3-strike timeout. */
#define DNS_POLL_EVERY  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)

/* Overall backstop in case the state machine wedges. */
#define DNS_DEADLINE    ((tiku_clock_time_t)(8u * TIKU_CLOCK_SECOND))

/* Default recursive resolver, reached through the SLIP host's relay/NAT. */
#ifndef TIKU_SHELL_DNS_SERVER
#define TIKU_SHELL_DNS_SERVER  {8, 8, 8, 8}
#endif

static uint8_t           dns_on;
static tiku_clock_time_t dns_t0;
static tiku_clock_time_t dns_last_poll;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

static uint8_t
dns_parse_ip(const char *s, uint8_t out[4])
{
    uint8_t i;

    for (i = 0; i < 4u; i++) {
        uint16_t v = 0;
        uint8_t  digits = 0;

        while (*s >= '0' && *s <= '9') {
            v = (uint16_t)(v * 10u + (uint16_t)(*s - '0'));
            if (v > 255u) {
                return 0;
            }
            s++;
            digits++;
        }
        if (digits == 0u) {
            return 0;
        }
        out[i] = (uint8_t)v;
        if (i < 3u) {
            if (*s != '.') {
                return 0;
            }
            s++;
        }
    }
    return (*s == '\0') ? 1u : 0u;
}

/*---------------------------------------------------------------------------*/
/* COMMAND + TICK                                                            */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_shell_cmd_dns_active(void)
{
    return dns_on;
}

void
tiku_shell_cmd_dns(uint8_t argc, const char *argv[])
{
    static uint8_t udp_ready;
    uint8_t server[4] = TIKU_SHELL_DNS_SERVER;

    if (dns_on) {
        SHELL_PRINTF("dns already running\n");
        return;
    }
    if (argc < 2) {
        SHELL_PRINTF("usage: dns <hostname> [resolver-ip]\n");
        return;
    }
    if (argc >= 3 && !dns_parse_ip(argv[2], server)) {
        SHELL_PRINTF("usage: dns <hostname> [resolver-ip]\n");
        return;
    }

    tiku_shell_cmd_slip_enable();
    if (!udp_ready) {
        tiku_kits_net_udp_init();
        udp_ready = 1;
    }

    tiku_kits_net_dns_init();
    tiku_kits_net_dns_set_server(server);
    if (tiku_kits_net_dns_resolve(argv[1]) != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("dns: bad query\n");
        return;
    }

    dns_on        = 1;
    dns_t0        = tiku_clock_time();
    dns_last_poll = dns_t0;
    SHELL_PRINTF("resolving %s via %u.%u.%u.%u ...\n", argv[1],
                 server[0], server[1], server[2], server[3]);
}

void
tiku_shell_cmd_dns_tick(void)
{
    tiku_kits_net_dns_state_t st;

    if (!dns_on) {
        return;
    }

    if ((tiku_clock_time_t)(tiku_clock_time() - dns_t0) >= DNS_DEADLINE) {
        (void)tiku_kits_net_dns_abort();
        SHELL_PRINTF("dns: timeout\n");
        dns_on = 0;
        return;
    }

    if ((tiku_clock_time_t)(tiku_clock_time() - dns_last_poll) < DNS_POLL_EVERY) {
        return;
    }
    dns_last_poll = tiku_clock_time();

    (void)tiku_kits_net_dns_poll();
    st = tiku_kits_net_dns_get_state();

    if (st == TIKU_KITS_NET_DNS_STATE_DONE) {
        uint8_t a[4];

        if (tiku_kits_net_dns_get_addr(a) == TIKU_KITS_NET_OK) {
            SHELL_PRINTF("%u.%u.%u.%u  (ttl %lus)\n",
                         a[0], a[1], a[2], a[3],
                         (unsigned long)tiku_kits_net_dns_get_ttl());
        } else {
            SHELL_PRINTF("dns: no address\n");
        }
        dns_on = 0;
    } else if (st == TIKU_KITS_NET_DNS_STATE_ERROR) {
        SHELL_PRINTF("dns: not found\n");
        dns_on = 0;
    }
}
