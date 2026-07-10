/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ping.c - "ping" command implementation (async ICMP echo)
 *
 * Concurrent, non-blocking ICMP echo over SLIP.  `ping` enables SLIP mode
 * (so the shell's shared RX demux routes frames to the IP stack), registers
 * an ICMP echo-reply callback, and sends one probe per shell tick, printing
 * RTT or a timeout.  Replies arrive through the shared RX path -- the shell
 * stays interactive the whole time.  After `count` probes it prints a summary
 * and releases the callback.
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
#include "tiku_shell_cmd_slip.h"                      /* tiku_shell_cmd_slip_enable */
#include <kernel/shell/tiku_shell.h>                  /* SHELL_PRINTF */
#include <kernel/timers/tiku_clock.h>                 /* tiku_clock_time */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_icmp.h>

/*---------------------------------------------------------------------------*/
/* CONFIG + STATE                                                            */
/*---------------------------------------------------------------------------*/

#define PING_ID             0x4954u   /* identifier echoed back in replies */
#define PING_PAYLOAD        16u
#define PING_DEFAULT_COUNT  4u
#define PING_TIMEOUT_TICKS  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)  /* ~1 s */

static uint8_t           ping_on;        /* a run is in progress */
static uint8_t           ping_dst[4];
static uint16_t          ping_count;
static uint16_t          ping_seq;       /* last seq sent (1-based) */
static uint16_t          ping_sent;
static uint16_t          ping_recv;
static uint8_t           ping_awaiting;  /* waiting for reply to ping_seq */
static tiku_clock_time_t ping_t0;        /* send time of current probe */
static volatile uint8_t  ping_got;       /* a reply arrived (set by callback) */
static volatile uint16_t ping_got_seq;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

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

/* ICMP echo-reply handler.  Runs in shell-loop context (shell RX demux ->
 * ipv4_input -> icmp_input), so it just records the match for the tick. */
static void
ping_on_reply(const uint8_t *src_ip, uint16_t id, uint16_t seq)
{
    (void)src_ip;
    if (id == PING_ID) {
        ping_got     = 1;
        ping_got_seq = seq;
    }
}

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
    ping_t0       = tiku_clock_time();
    ping_awaiting = 1;
    ping_got      = 0;
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

    /* Make sure SLIP mode is on so the shared RX demux delivers our replies,
     * and hook the ICMP echo-reply callback. */
    tiku_shell_cmd_slip_enable();
    tiku_kits_net_icmp_set_reply_cb(ping_on_reply);

    ping_seq  = 0;
    ping_sent = 0;
    ping_recv = 0;
    ping_on   = 1;

    SHELL_PRINTF("PING %u.%u.%u.%u  %u packets\n",
                 ping_dst[0], ping_dst[1], ping_dst[2], ping_dst[3],
                 ping_count);
    ping_send_probe();
}

void
tiku_shell_cmd_ping_tick(void)
{
    if (!ping_on) {
        return;
    }

    /* Matching reply for the current probe? */
    if (ping_awaiting && ping_got && ping_got_seq == ping_seq) {
        tiku_clock_time_t dt = (tiku_clock_time_t)(tiku_clock_time() - ping_t0);
        unsigned long ms = (unsigned long)dt * 1000UL /
                           (unsigned long)TIKU_CLOCK_SECOND;

        ping_recv++;
        SHELL_PRINTF("  reply from %u.%u.%u.%u seq=%u time=%lums\n",
                     ping_dst[0], ping_dst[1], ping_dst[2], ping_dst[3],
                     ping_got_seq, ms);
        ping_awaiting = 0;
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
            tiku_kits_net_icmp_set_reply_cb((tiku_kits_net_icmp_reply_cb_t)0);
            ping_on = 0;   /* the shell loop restores the prompt */
        } else {
            ping_send_probe();
        }
    }
}
