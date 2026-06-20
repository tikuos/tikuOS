/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ping.c - "ping" command implementation
 *
 * Non-blocking ICMP echo over SLIP.  Net is a compile-time choice
 * (TIKU_KIT_NET_ENABLE=1); this is the runtime diagnostic.  A probe is sent,
 * and each shell poll tick the SLIP receiver is pumped for the echo reply
 * (round-trip time printed) or a per-probe timeout.  After `count` probes a
 * summary is printed and the prompt returns.  While active the UART carries
 * binary SLIP, so the shell yields all input to the ping engine -- the run is
 * bounded by `count` (reset the board to abort early).
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

#include "tiku_shell_cmd_ping.h"
#include <kernel/shell/tiku_shell.h>                  /* SHELL_PRINTF */
#include <kernel/timers/tiku_clock.h>                 /* tiku_clock_time */
#include <tikukits/net/tiku_kits_net.h>               /* MTU, IP default */
#include <tikukits/net/slip/tiku_kits_net_slip.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_icmp.h>

/*---------------------------------------------------------------------------*/
/* CONFIG + STATE                                                            */
/*---------------------------------------------------------------------------*/

#define PING_ID             0x4954u   /* identifier echoed back in replies */
#define PING_PAYLOAD        16u       /* echo payload bytes */
#define PING_DEFAULT_COUNT  4u
#define PING_TIMEOUT_TICKS  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)  /* ~1 s */

static uint8_t           ping_on;        /* mode active */
static uint8_t           ping_dst[4];    /* target address */
static uint16_t          ping_count;     /* total probes to send */
static uint16_t          ping_seq;       /* last seq sent (1-based) */
static uint16_t          ping_sent;
static uint16_t          ping_recv;
static uint8_t           ping_awaiting;  /* waiting for reply to ping_seq */
static tiku_clock_time_t ping_t0;        /* send time of current probe */
static uint16_t          ping_rx_pos;    /* SLIP decode position */
static uint8_t           ping_rx[TIKU_KITS_NET_MTU];

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/* Parse "a.b.c.d" into out[4].  Returns 1 on success, 0 if malformed. */
static uint8_t
ping_parse_ip(const char *s, uint8_t out[4])
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

static uint16_t
ping_parse_u16(const char *s)
{
    uint16_t v = 0;

    while (*s >= '0' && *s <= '9') {
        v = (uint16_t)(v * 10u + (uint16_t)(*s - '0'));
        s++;
    }
    return v;
}

/* Build + transmit the next echo request (advances ping_seq). */
static void
ping_send_probe(void)
{
    uint8_t  *buf;
    uint16_t  size;
    uint16_t  len;

    buf = tiku_kits_net_ipv4_get_buf(&size);
    ping_seq++;
    len = tiku_kits_net_icmp_build_echo_request(buf, ping_dst, PING_ID,
                                                ping_seq, PING_PAYLOAD);
    ping_rx_pos   = 0;
    ping_t0       = tiku_clock_time();
    ping_awaiting = 1;
    ping_sent++;
    (void)tiku_kits_net_ipv4_output(buf, len);
}

/*---------------------------------------------------------------------------*/
/* COMMAND + TICK                                                            */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_shell_cmd_ping_active(void)
{
    return ping_on;
}

void
tiku_shell_cmd_ping(uint8_t argc, const char *argv[])
{
    static const uint8_t self[4] = TIKU_KITS_NET_IP_ADDR;

    if (ping_on) {
        SHELL_PRINTF("ping already running\n");
        return;
    }
    if (argc < 2 || !ping_parse_ip(argv[1], ping_dst)) {
        SHELL_PRINTF("usage: ping <a.b.c.d> [count]\n");
        return;
    }

    ping_count = (argc >= 3) ? ping_parse_u16(argv[2]) : PING_DEFAULT_COUNT;
    if (ping_count == 0u) {
        ping_count = 1u;
    }

    /* Bring up SLIP on the console UART and ensure a valid source addr
     * (idempotent -- safe whether or not the net process ran before). */
    tiku_kits_net_slip_init();
    tiku_kits_net_ipv4_set_link(&tiku_kits_net_slip_link);
    tiku_kits_net_ipv4_set_addr(self);

    ping_seq  = 0;
    ping_sent = 0;
    ping_recv = 0;
    ping_on   = 1;

    SHELL_PRINTF("PING %u.%u.%u.%u  %u packets (reset to abort)\n",
                 ping_dst[0], ping_dst[1], ping_dst[2], ping_dst[3],
                 ping_count);
    ping_send_probe();
}

void
tiku_shell_cmd_ping_tick(void)
{
    uint16_t seq = 0;

    if (!ping_on) {
        return;
    }

    /* Pump the SLIP receiver; on a full frame, test for our echo reply. */
    if (ping_awaiting &&
        tiku_kits_net_slip_poll_rx(ping_rx, (uint16_t)sizeof ping_rx,
                                   &ping_rx_pos)) {
        uint16_t len = ping_rx_pos;

        ping_rx_pos = 0;  /* ready for the next frame */
        if (tiku_kits_net_icmp_match_echo_reply(ping_rx, len, PING_ID, &seq)) {
            tiku_clock_time_t dt = (tiku_clock_time_t)(tiku_clock_time() -
                                                       ping_t0);
            unsigned long ms = (unsigned long)dt * 1000UL /
                               (unsigned long)TIKU_CLOCK_SECOND;

            ping_recv++;
            SHELL_PRINTF("  reply from %u.%u.%u.%u seq=%u time=%lums\n",
                         ping_dst[0], ping_dst[1], ping_dst[2], ping_dst[3],
                         seq, ms);
            ping_awaiting = 0;
        }
        /* A non-matching frame is ignored; keep waiting. */
    }

    /* Per-probe timeout. */
    if (ping_awaiting &&
        (tiku_clock_time_t)(tiku_clock_time() - ping_t0) >= PING_TIMEOUT_TICKS) {
        SHELL_PRINTF("  seq=%u timeout\n", ping_seq);
        ping_awaiting = 0;
    }

    /* Advance to the next probe, or finish the run. */
    if (!ping_awaiting) {
        if (ping_seq >= ping_count) {
            SHELL_PRINTF("--- %u.%u.%u.%u ping: %u sent, %u received ---\n",
                         ping_dst[0], ping_dst[1], ping_dst[2], ping_dst[3],
                         ping_sent, ping_recv);
            ping_on = 0;   /* the shell loop restores the prompt */
        } else {
            ping_send_probe();
        }
    }
}
