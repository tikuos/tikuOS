/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_net.inl - networking words for the full BASIC profile
 * (RP2350 / Apollo): UDPSEND (instant), MQTTPUB (tick-driven via a pump
 * helper), HTTPGET$ (bounded blocking). The numeric/string accessors
 * (IPADDR$, NETUP, HTTPGET$, HTTPSTATUS) live in tiku_basic_string.inl /
 * tiku_basic_call.inl; the statements + the cooperative pump live here.
 *
 * Cooperative-blocking rule: exec_run only pumps the console between
 * statements, so any net op that waits MUST pump the console itself or it
 * hard-hangs the board (the ADC-hang class). The MQTT path pumps via
 * basic_net_mqtt_pump(); UDP is instant; HTTP self-pumps the net stack.
 *
 * NOT a standalone translation unit -- included from tiku_basic.c after
 * tiku_basic_stmt.inl (it reuses parse_strexpr / parse_path_literal).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_NET_ENABLE

/* Parse a dotted-quad "a.b.c.d" into 4 bytes. Returns 0 on success. */
static int
basic_net_parse_ip(const char *s, uint8_t out[4])
{
    int i, digits;
    long v;
    for (i = 0; i < 4; i++) {
        v = 0; digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            if (v > 255) return -1;
            s++; digits++;
        }
        if (digits == 0) return -1;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return -1; s++; }
    }
    return (*s == '\0') ? 0 : -1;
}

/* UDPSEND "a.b.c.d", port, expr$ -- fire-and-forget a datagram. Instant
 * (the stack queues + transmits synchronously), so no pump needed. */
static void
exec_udpsend(const char **p)
{
    char    ipstr[20];
    char    payload[TIKU_BASIC_STR_BUF_CAP];
    uint8_t ip[4];
    long    port;

    if (parse_path_literal(p, ipstr, sizeof(ipstr)) != 0) return;
    if (basic_net_parse_ip(ipstr, ip) != 0) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? bad IP '%s'\n" SH_RST, ipstr); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    port = parse_expr(p);
    if (basic_error) return;
    if (port < 1 || port > 65535) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? UDP port out of range\n" SH_RST); return;
    }
    skip_ws(p);
    if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return; }
    (*p)++;
    if (parse_strexpr(p, payload, sizeof(payload)) != 0) return;
    if (tiku_kits_net_udp_send(ip, (uint16_t)port, 5000U,
                               (const uint8_t *)payload,
                               (uint16_t)strlen(payload)) != TIKU_KITS_NET_OK) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? UDP send failed (is the IP link up? 'wifi up')\n"
                     SH_RST);
    }
}

#endif /* TIKU_BASIC_NET_ENABLE */
