/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ntp.c - "ntp" command implementation (async SNTP query)
 *
 * Fetches wall-clock time from an SNTP server over SLIP and sets the system
 * RTC (tiku_rtc_set_seconds) so date-dependent consumers -- TLS certificate
 * validity, /sys/time, BASIC DATE$/NOW -- get a real clock.  Accepts either a
 * dotted IPv4 address or a hostname; a hostname is resolved first via the DNS
 * stub resolver (a public resolver reached through the SLIP host's relay/NAT),
 * then the SNTP query is sent to the resolved address.  With no argument it
 * queries a default public server (TIKU_SHELL_NTP_SERVER, time.google.com) so
 * a bare `ntp` works over a real internet bridge.  Both the DNS and NTP
 * libraries are poll-based and treat each no-reply poll() as one retry toward
 * a 3-strike timeout, so this command paces polling at ~1 Hz (the cadence the
 * libraries document) rather than once per shell tick.  All I/O flows through
 * the shell's shared RX demux, so the shell stays interactive throughout.
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

#include "tiku_shell_cmd_ntp.h"
#include "tiku_shell_cmd_slip.h"                  /* tiku_shell_cmd_slip_enable */
#include <kernel/shell/tiku_shell.h>              /* SHELL_PRINTF */
#include <kernel/timers/tiku_clock.h>             /* tiku_clock_time */
#include <tikukits/net/tiku_kits_net.h>           /* TIKU_KITS_NET_OK, etc. */
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>  /* udp_init */
#include <tikukits/net/ipv4/tiku_kits_net_dns.h>  /* DNS stub resolver */
#include <tikukits/time/ntp/tiku_kits_time_ntp.h>
#include <tikukits/time/tiku_kits_time.h>         /* tiku_kits_time_tm_t */
#include <kernel/cpu/tiku_rtc.h>                  /* tiku_rtc_set_seconds */

/*---------------------------------------------------------------------------*/
/* CONFIG + STATE                                                            */
/*---------------------------------------------------------------------------*/

/* Poll the (poll-based) DNS/NTP clients at the cadence they expect (~1 s).
 * Each no-reply poll() counts as one retry toward their 3-strike timeout, so
 * polling every shell tick would time out in milliseconds. */
#define NTP_POLL_EVERY  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)

/* Overall per-phase backstop in case a state machine wedges. */
#define NTP_DEADLINE    ((tiku_clock_time_t)(12u * TIKU_CLOCK_SECOND))

/* Public DNS resolver used for hostname lookups, reached through the SLIP
 * host's relay/NAT.  Override at build time if needed. */
#ifndef TIKU_SHELL_NTP_DNS_SERVER
#define TIKU_SHELL_NTP_DNS_SERVER  {8, 8, 8, 8}
#endif

/* Default NTP server for a bare `ntp` (no argument).  The SLIP host (.1) only
 * answers NTP under the TikuBench test harness; for interactive use over a
 * real internet bridge, default to a public server instead.  time.google.com
 * anycast answers SNTP directly (no DNS needed) and is the very address the
 * SNTP client header documents as its example.  Override at build time. */
#ifndef TIKU_SHELL_NTP_SERVER
#define TIKU_SHELL_NTP_SERVER  {216, 239, 35, 0}
#endif

typedef enum {
    NTP_PH_IDLE,    /* nothing in flight */
    NTP_PH_DNS,     /* resolving a hostname */
    NTP_PH_NTP      /* awaiting the SNTP reply */
} ntp_phase_t;

static ntp_phase_t       ntp_phase;
static uint8_t           ntp_srv[4];    /* NTP server (resolved or given) */
static tiku_clock_time_t ntp_t0;        /* current phase start */
static tiku_clock_time_t ntp_last_poll; /* last library poll (for pacing) */

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

static uint8_t
ntp_parse_ip(const char *s, uint8_t out[4])
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

/* Print a zero-padded two-digit field without relying on printf width
 * specifiers (SHELL_PRINTF is intentionally minimal). */
static void
ntp_put2(uint8_t v)
{
    SHELL_PRINTF("%u%u", (unsigned)((v / 10u) % 10u), (unsigned)(v % 10u));
}

/* Begin (or restart) the SNTP query phase against ntp_srv. */
static void
ntp_begin_query(void)
{
    tiku_kits_time_ntp_init();
    if (tiku_kits_time_ntp_request(ntp_srv) != TIKU_KITS_TIME_OK) {
        SHELL_PRINTF("ntp: send failed\n");
        ntp_phase = NTP_PH_IDLE;
        return;
    }
    ntp_phase     = NTP_PH_NTP;
    ntp_t0        = tiku_clock_time();
    ntp_last_poll = ntp_t0;
    SHELL_PRINTF("NTP query to %u.%u.%u.%u ...\n",
                 ntp_srv[0], ntp_srv[1], ntp_srv[2], ntp_srv[3]);
}

/*---------------------------------------------------------------------------*/
/* COMMAND + TICK                                                            */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_shell_cmd_ntp_active(void)
{
    return (uint8_t)(ntp_phase != NTP_PH_IDLE);
}

void
tiku_shell_cmd_ntp(uint8_t argc, const char *argv[])
{
    static uint8_t udp_ready;   /* one-shot UDP bring-up for standalone builds */

    if (ntp_phase != NTP_PH_IDLE) {
        SHELL_PRINTF("ntp already running\n");
        return;
    }

    /* SLIP on so the shared RX demux delivers replies; ensure UDP dispatch
     * is up (the net-test path already inits it; this covers shell+net). */
    tiku_shell_cmd_slip_enable();
    if (!udp_ready) {
        tiku_kits_net_udp_init();
        udp_ready = 1;
    }

    if (argc >= 2) {
        if (ntp_parse_ip(argv[1], ntp_srv)) {
            ntp_begin_query();              /* dotted IPv4 -> query directly */
        } else {
            /* Hostname -> resolve via DNS first, then query. */
            static const uint8_t resolver[4] = TIKU_SHELL_NTP_DNS_SERVER;
            tiku_kits_net_dns_init();
            tiku_kits_net_dns_set_server(resolver);
            if (tiku_kits_net_dns_resolve(argv[1]) != TIKU_KITS_NET_OK) {
                SHELL_PRINTF("ntp: bad host\n");
                return;
            }
            ntp_phase     = NTP_PH_DNS;
            ntp_t0        = tiku_clock_time();
            ntp_last_poll = ntp_t0;
            SHELL_PRINTF("resolving %s via %u.%u.%u.%u ...\n", argv[1],
                         resolver[0], resolver[1], resolver[2], resolver[3]);
        }
        return;
    }

    /* No argument: query the default public NTP server.  Override the target
     * with `ntp <ip|host>`, or query the SLIP host with `ntp 172.16.7.1`. */
    {
        static const uint8_t def[4] = TIKU_SHELL_NTP_SERVER;
        ntp_srv[0] = def[0];
        ntp_srv[1] = def[1];
        ntp_srv[2] = def[2];
        ntp_srv[3] = def[3];
    }
    ntp_begin_query();
}

void
tiku_shell_cmd_ntp_tick(void)
{
    if (ntp_phase == NTP_PH_IDLE) {
        return;
    }

    /* Per-phase backstop. */
    if ((tiku_clock_time_t)(tiku_clock_time() - ntp_t0) >= NTP_DEADLINE) {
        if (ntp_phase == NTP_PH_DNS) {
            (void)tiku_kits_net_dns_abort();
        } else {
            (void)tiku_kits_time_ntp_abort();
        }
        SHELL_PRINTF("ntp: timeout\n");
        ntp_phase = NTP_PH_IDLE;
        return;
    }

    /* Pace the library polls (~1 Hz). */
    if ((tiku_clock_time_t)(tiku_clock_time() - ntp_last_poll) < NTP_POLL_EVERY) {
        return;
    }
    ntp_last_poll = tiku_clock_time();

    if (ntp_phase == NTP_PH_DNS) {
        tiku_kits_net_dns_state_t st;

        (void)tiku_kits_net_dns_poll();
        st = tiku_kits_net_dns_get_state();

        if (st == TIKU_KITS_NET_DNS_STATE_DONE) {
            if (tiku_kits_net_dns_get_addr(ntp_srv) == TIKU_KITS_NET_OK) {
                SHELL_PRINTF("resolved -> %u.%u.%u.%u\n",
                             ntp_srv[0], ntp_srv[1], ntp_srv[2], ntp_srv[3]);
                ntp_begin_query();          /* advance to the NTP phase */
            } else {
                SHELL_PRINTF("ntp: resolve error\n");
                ntp_phase = NTP_PH_IDLE;
            }
        } else if (st == TIKU_KITS_NET_DNS_STATE_ERROR) {
            SHELL_PRINTF("ntp: dns failed\n");
            ntp_phase = NTP_PH_IDLE;
        }
        return;
    }

    /* NTP phase. */
    {
        tiku_kits_time_ntp_state_t st;

        (void)tiku_kits_time_ntp_poll();
        st = tiku_kits_time_ntp_get_state();

        if (st == TIKU_KITS_TIME_NTP_STATE_DONE) {
            tiku_kits_time_tm_t   tm;
            tiku_kits_time_unix_t ts;

            if (tiku_kits_time_ntp_get_tm(&tm) == TIKU_KITS_TIME_OK &&
                tiku_kits_time_ntp_get_time(&ts) == TIKU_KITS_TIME_OK) {
                /* Set the system wall clock so date-dependent consumers (TLS
                 * certificate validity, /sys/time, BASIC DATE$/NOW) have a real
                 * time without a separate `write /sys/time`. */
                tiku_rtc_set_seconds((uint32_t)ts);
                SHELL_PRINTF("ntp: %u-", (unsigned)tm.year);
                ntp_put2(tm.month);
                SHELL_PRINTF("-");
                ntp_put2(tm.day);
                SHELL_PRINTF(" ");
                ntp_put2(tm.hour);
                SHELL_PRINTF(":");
                ntp_put2(tm.minute);
                SHELL_PRINTF(":");
                ntp_put2(tm.second);
                SHELL_PRINTF(" UTC  stratum %u  (clock set)\n",
                             (unsigned)tiku_kits_time_ntp_get_stratum());
            } else {
                SHELL_PRINTF("ntp: reply parse error\n");
            }
            ntp_phase = NTP_PH_IDLE;
        } else if (st == TIKU_KITS_TIME_NTP_STATE_ERROR) {
            SHELL_PRINTF("ntp: no reply (error)\n");
            ntp_phase = NTP_PH_IDLE;
        }
    }
}
