/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ntp.c - "ntp" command (async SNTP query).
 *
 * Fetches wall-clock time over SNTP and sets the RTC, which TLS certificate
 * checks, /sys/time and the BASIC date functions read.  A hostname is resolved
 * by DNS first, and the query runs across shell ticks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_ntp.h"
#include "tiku_shell_cmd_slip.h"                  /* slip_enable */
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

/* The DNS and NTP clients count each no-reply poll() as one retry toward a
 * 3-retry timeout, so the tick polls them once per second. */
#define NTP_POLL_EVERY  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)

/* A phase still running after this long is aborted as a timeout. */
#define NTP_DEADLINE    ((tiku_clock_time_t)(12u * TIKU_CLOCK_SECOND))

/* Server for a bare `ntp`: 216.239.35.0, a time.google.com anycast address.
 * A build overrides it with -DTIKU_SHELL_NTP_SERVER={a,b,c,d}. */
#ifndef TIKU_SHELL_NTP_SERVER
#define TIKU_SHELL_NTP_SERVER  {216, 239, 35, 0}
#endif

typedef enum {
    NTP_PH_IDLE,    /**< nothing in flight */
    NTP_PH_DNS,     /**< resolving a hostname */
    NTP_PH_NTP      /**< awaiting the SNTP reply */
} ntp_phase_t;

static ntp_phase_t       ntp_phase;
static uint8_t           ntp_srv[4];    /* NTP server (resolved or given) */
static tiku_clock_time_t ntp_t0;        /* current phase start */
static tiku_clock_time_t ntp_last_poll; /* last library poll (for pacing) */

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Parse dotted IPv4 @p s into @p out; 1 on success, 0 otherwise. */
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

/** @brief Send an SNTP request to ntp_srv and enter the NTP phase; on a send
 *         failure, print it and go idle. */
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
    static uint8_t udp_ready;   /* this command has initialised UDP */

    if (ntp_phase != NTP_PH_IDLE) {
        SHELL_PRINTF("ntp already running\n");
        return;
    }

    /* Turn SLIP on so the console's IPv4 channel delivers the reply, and
     * initialise UDP dispatch on the first call. */
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
            uint8_t resolver[4];
            /* The set override, else the DHCP lease's DNS, else 8.8.8.8. */
            tiku_kits_net_dns_default_server(resolver);
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

    /* No argument: query TIKU_SHELL_NTP_SERVER.  `ntp <ip|host>` names
     * another server; `ntp 172.16.7.1` queries the SLIP host, which answers
     * NTP under the TikuBench harness. */
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

    /* Abort a phase that has run for NTP_DEADLINE. */
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
                /* Set the RTC; TLS certificate validity, /sys/time and the
                 * BASIC functions DATE$ and NOW() read it. */
                int saved = tiku_rtc_set_seconds_status((uint32_t)ts);
                SHELL_PRINTF("ntp: %u-%02u-%02u %02u:%02u:%02u",
                             (unsigned)tm.year, (unsigned)tm.month,
                             (unsigned)tm.day, (unsigned)tm.hour,
                             (unsigned)tm.minute, (unsigned)tm.second);
                SHELL_PRINTF(" UTC  stratum %u  (%s)\n",
                             (unsigned)tiku_kits_time_ntp_get_stratum(),
                             saved == 0 ? "clock set" : "clock persistence failed");
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
