/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_radio154.c - 802.15.4 PHY bring-up command (N1).  Thin
 * veneer over arch/nordic/tiku_ieee154_arch.h; opt-in via
 * TIKU_SHELL_CMD_RADIO154=1 and gated on TIKU_HAS_154 (nRF54L on-die RADIO).
 *
 * Mirrors the `bleadv` bring-up discipline: readbacks first-class, two-board
 * TikuOS<->TikuOS the oracle.  The demo frame is a raw PHY payload tagged
 * "TK15" (MAC framing/addressing is N2); the peer's `radio154 rx` labels a
 * matching tag so a busy 2.4 GHz band cannot spoof a pass.
 *
 *   radio154 tx [ch] [text]   send one frame (default ch 15, seq++)
 *   radio154 rx [ch] [secs]   listen; dump CRC-OK frames (default ch 15, 10 s)
 *   radio154 ed [ch]          energy detect one channel, or scan 11..26
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_radio154.h"

#include <string.h>
#include <kernel/shell/tiku_shell_io.h>

#if (TIKU_HAS_154 + 0)

#include <arch/nordic/tiku_ieee154_arch.h>
#include <arch/nordic/tiku_radio_arch.h>       /* constlat hold around bursts  */
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND        */
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>

/* Payload tag so the peer can tell OUR frames from ambient 15.4 traffic. */
static const uint8_t tk15_tag[4] = { 'T', 'K', '1', '5' };
static uint8_t tk15_seq;

/* Tiny non-negative decimal parser (no shell helper for this). */
static long r154_atoi(const char *s)
{
    long v = 0;
    if (s == (const char *)0) {
        return -1;
    }
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (*s - '0');
        s++;
    }
    return (*s == '\0') ? v : -1;
}

static uint8_t r154_channel(const char *arg, uint8_t dflt)
{
    long c = (arg != (const char *)0) ? r154_atoi(arg) : -1;
    if (c < (long)TIKU_154_CHAN_MIN || c > (long)TIKU_154_CHAN_MAX) {
        return dflt;
    }
    return (uint8_t)c;
}

static void r154_hex(char *out, const uint8_t *b, uint8_t n)
{
    static const char hx[] = "0123456789ABCDEF";
    uint8_t i;
    for (i = 0u; i < n; i++) {
        out[i * 2u]      = hx[(b[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = hx[b[i] & 0xFu];
    }
    out[n * 2u] = '\0';
}

static void r154_tx(uint8_t argc, const char *argv[])
{
    uint8_t ch = r154_channel(argc > 2u ? argv[2] : (const char *)0, 15u);
    const char *text = (argc > 3u) ? argv[3] : "hello";
    uint8_t frame[TIKU_154_MAX_PSDU];
    uint8_t n = 0u;
    int rc;
    size_t tlen = strlen(text);

    memcpy(frame, tk15_tag, sizeof(tk15_tag));
    n = (uint8_t)sizeof(tk15_tag);
    frame[n++] = tk15_seq++;
    if (tlen > (size_t)(TIKU_154_MAX_PSDU - n)) {
        tlen = (size_t)(TIKU_154_MAX_PSDU - n);
    }
    memcpy(&frame[n], text, tlen);
    n = (uint8_t)(n + tlen);

    tiku_ieee154_arch_mode_154(ch);
    tiku_radio_arch_constlat_hold(1);           /* erratum 20 before TXEN     */
    rc = tiku_ieee154_arch_tx(frame, n);
    tiku_radio_arch_constlat_hold(0);
    tiku_ieee154_arch_mode_ble();               /* hand the RADIO back to BLE */

    if (rc == 0) {
        SHELL_PRINTF("154 TX ch%u %u B ok (seq=%u '%s')\n",
                     (unsigned)ch, (unsigned)n, (unsigned)(tk15_seq - 1u),
                     text);
    } else {
        SHELL_PRINTF("154 TX ch%u failed rc=%d\n", (unsigned)ch, rc);
    }
}

static void r154_rx(uint8_t argc, const char *argv[])
{
    uint8_t ch = r154_channel(argc > 2u ? argv[2] : (const char *)0, 15u);
    long secs = (argc > 3u) ? r154_atoi(argv[3]) : 10;
    uint8_t buf[TIKU_154_MAX_PSDU];
    char hx[TIKU_154_MAX_PSDU * 2u + 1u];
    tiku_clock_time_t start;
    uint32_t heard = 0u, ours = 0u, badcrc = 0u;

    if (secs <= 0 || secs > 120) {
        secs = 10;
    }
    SHELL_PRINTF("154 RX ch%u listening ~%ld s (tag TK15)...\n",
                 (unsigned)ch, secs);
    tiku_ieee154_arch_mode_154(ch);
    tiku_radio_arch_constlat_hold(1);
    start = tiku_clock_time();
    while ((tiku_clock_time_t)(tiku_clock_time() - start) <
           (tiku_clock_time_t)((uint32_t)TIKU_CLOCK_SECOND * (uint32_t)secs)) {
        int8_t rssi = 0;
        int n = tiku_ieee154_arch_rx(buf, sizeof(buf), 500u, &rssi);
        tiku_watchdog_kick();
        if (n > 0) {
            uint8_t mine = (n >= 4 &&
                            memcmp(buf, tk15_tag, sizeof(tk15_tag)) == 0);
            heard++;
            if (mine) {
                ours++;
            }
            r154_hex(hx, buf, (uint8_t)(n > 24 ? 24 : n));
            SHELL_PRINTF("  RX %d B rssi=%d dBm %s [%s]\n",
                         n, (int)rssi, mine ? "TK15" : "----", hx);
        } else if (n < 0) {
            badcrc++;
        }
    }
    tiku_radio_arch_constlat_hold(0);
    tiku_ieee154_arch_mode_ble();
    SHELL_PRINTF("154 RX done: %lu frames (%lu ours, %lu bad-FCS)\n",
                 (unsigned long)heard, (unsigned long)ours,
                 (unsigned long)badcrc);
}

static void r154_ed(uint8_t argc, const char *argv[])
{
    if (argc > 2u) {
        uint8_t ch = r154_channel(argv[2], 15u);
        int8_t dbm = 0;
        int lvl;
        tiku_radio_arch_constlat_hold(1);
        lvl = tiku_ieee154_arch_ed(ch, &dbm);
        tiku_radio_arch_constlat_hold(0);
        tiku_ieee154_arch_mode_ble();
        SHELL_PRINTF("154 ED ch%u: level=%d (~%d dBm)\n",
                     (unsigned)ch, lvl, (int)dbm);
        return;
    }
    SHELL_PRINTF("154 ED scan ch11..26:\n");
    tiku_radio_arch_constlat_hold(1);
    {
        uint8_t ch;
        for (ch = TIKU_154_CHAN_MIN; ch <= TIKU_154_CHAN_MAX; ch++) {
            int8_t dbm = 0;
            int lvl = tiku_ieee154_arch_ed(ch, &dbm);
            tiku_watchdog_kick();
            SHELL_PRINTF("  ch%2u  level=%3d  ~%d dBm\n",
                         (unsigned)ch, lvl, (int)dbm);
        }
    }
    tiku_radio_arch_constlat_hold(0);
    tiku_ieee154_arch_mode_ble();
}

void tiku_shell_cmd_radio154(uint8_t argc, const char *argv[])
{
    if (argc < 2u) {
        SHELL_PRINTF("usage: radio154 tx [ch] [text] | rx [ch] [secs]"
                     " | ed [ch]\n");
        return;
    }
    if (strcmp(argv[1], "tx") == 0) {
        r154_tx(argc, argv);
    } else if (strcmp(argv[1], "rx") == 0) {
        r154_rx(argc, argv);
    } else if (strcmp(argv[1], "ed") == 0) {
        r154_ed(argc, argv);
    } else {
        SHELL_PRINTF("radio154: unknown '%s' (tx|rx|ed)\n", argv[1]);
    }
}

#else /* !TIKU_HAS_154 */

void tiku_shell_cmd_radio154(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    SHELL_PRINTF("radio154: 802.15.4 PHY not built for this target\n");
}

#endif /* TIKU_HAS_154 */
