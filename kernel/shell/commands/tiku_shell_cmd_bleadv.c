/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bleadv.c - BLE beacon/scan shell command (broadcast facade).
 *
 * Opt-in (TIKU_SHELL_CMD_BLEADV=1 via EXTRA_CFLAGS); needs a broadcast-
 * capable radio (TIKU_HAS_BLE_ADV -- the nRF54L15 on-die RADIO today).
 * Thin veneer over interfaces/bluetooth/tiku_ble_adv.h; the same facade
 * backs the BASIC BLEBEACON/BLESCAN$ words and the /sys/radio VFS nodes,
 * so anything proven here holds for those too.
 *
 *   bleadv <name> [secs]   demo beacon for ~secs (100 ms bursts, auto-stop)
 *   bleadv on <name> [ms]  background beacon (kernel timer; system sleeps
 *                          between bursts) until `bleadv off`
 *   bleadv off             stop the background beacon
 *   bleadv scan [secs] [prefix]
 *                          passive scan; lists addr/rssi/type/name.  With a
 *                          prefix only advertisers whose name starts with it
 *                          are kept (insert-time filter, so ambient traffic
 *                          cannot flood the table -- the TikuBench
 *                          reverse-nonce oracle's knob)
 *   bleadv observe [secs]  BACKGROUND observer: the IRQ+hw-window engine
 *                          scans while the shell stays interactive; live
 *                          results in /sys/radio/scan (cat it, watch it,
 *                          or hang a rule on it).  secs 0/absent = until
 *                          `bleadv observe off`
 *   bleadv ext <name> [secs]
 *                          extended advertising (R8.3a): ADV_EXT_IND +
 *                          hardware-timed AUX_ADV_IND carrying >31-byte
 *                          AdvData; dbg_aux_us proves the AuxPtr timing
 *   bleadv phy             probe all four BLE PHYs (1M/2M/S8/S2) with one
 *                          3-channel burst each; prints TX-window iteration
 *                          counts + ratios vs 1M.  ON-DIE proof only:
 *                          legacy adv is 1M-only by spec, so 2M/coded
 *                          bursts are inaudible to compliant scanners
 *                          (kintsugi/radio.md R8.1)
 *   bleadv dbg             silicon + clock + radio readbacks (bring-up)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_bleadv.h"

#if TIKU_SHELL_CMD_BLEADV

#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_timer_arch.h>   /* TIKU_CLOCK_ARCH_SECOND before clock.h */
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>      /* demo auto-stop callback timer */
#include <interfaces/bluetooth/tiku_ble_adv.h>
#include <arch/nordic/tiku_radio_arch.h>     /* per-burst dbg counters (dbg)    */
#include <arch/nordic/tiku_device_select.h>  /* NRF_CLOCK_S / NRF_RADIO_S (dbg) */
#include <arch/nordic/tiku_nordic_core.h>    /* wfe (ext pacing)                */
#include <kernel/cpu/tiku_common.h>          /* unique id -> AdvA (ext)         */
#include <kernel/cpu/tiku_watchdog.h>        /* kick across the ext loop        */
#include <stdlib.h>
#include <string.h>

/* SHELL_PRINTF has no %02X, so format an address (host order: MSB first)
 * into "AA:BB:CC:DD:EE:FF" ourselves. */
static void bleadv_fmt_addr(char *out, const uint8_t addr[6])
{
    static const char hex[] = "0123456789ABCDEF";
    int i, o = 0;
    for (i = 5; i >= 0; i--) {
        out[o++] = hex[addr[i] >> 4];
        out[o++] = hex[addr[i] & 0x0Fu];
        if (i) {
            out[o++] = ':';
        }
    }
    out[o] = '\0';
}

/* Bring-up visibility: silicon identification words that gate the errata
 * workarounds (see tiku_crt_early.c), clock-tuning state the radio depends
 * on, and RADIO readbacks.  Everything here is a plain register read. */
static void bleadv_dbg(void)
{
    SHELL_PRINTF("FICR : part=%lx rev=%lx trimv=%lx\n",
                 *(volatile unsigned long *)0x00FFC340ul,
                 *(volatile unsigned long *)0x00FFC344ul,
                 *(volatile unsigned long *)0x00FFC334ul);
    SHELL_PRINTF("CLOCK: xostarted=%lu xotuned=%lu tuneerr=%lu tunefail=%lu\n",
                 (unsigned long)NRF_CLOCK_S->EVENTS_XOSTARTED,
                 (unsigned long)NRF_CLOCK_S->EVENTS_XOTUNED,
                 (unsigned long)NRF_CLOCK_S->EVENTS_XOTUNEERROR,
                 (unsigned long)NRF_CLOCK_S->EVENTS_XOTUNEFAILED);
    SHELL_PRINTF("       pllstarted=%lu xo.run=%lu pll.run=%lu pll.freq=%lx\n",
                 (unsigned long)NRF_CLOCK_S->EVENTS_PLLSTARTED,
                 (unsigned long)NRF_CLOCK_S->XO.RUN,
                 (unsigned long)NRF_CLOCK_S->PLL.RUN,
                 (unsigned long)NRF_OSCILLATORS_S->PLL.CURRENTFREQ);
    SHELL_PRINTF("RADIO: state=%lu mode=%lu txpower=%lx datawhite=%lx\n",
                 (unsigned long)NRF_RADIO_S->STATE,
                 (unsigned long)NRF_RADIO_S->MODE,
                 (unsigned long)NRF_RADIO_S->TXPOWER,
                 (unsigned long)NRF_RADIO_S->DATAWHITE);
    SHELL_PRINTF("FACADE: active=%d name=%s interval=%u bursts=%lu\n",
                 tiku_ble_adv_active(), tiku_ble_adv_name(),
                 (unsigned)tiku_ble_adv_interval_ms(),
                 (unsigned long)tiku_ble_adv_bursts());
    SHELL_PRINTF("BURST : ready=%lu disabled=%lu state=%lu spin=%lu "
                 "ru=%lu tx=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_ready,
                 (unsigned long)tiku_radio_arch_dbg_disabled,
                 (unsigned long)tiku_radio_arch_dbg_state,
                 (unsigned long)tiku_radio_arch_dbg_spin,
                 (unsigned long)tiku_radio_arch_dbg_ru_iters,
                 (unsigned long)tiku_radio_arch_dbg_tx_iters);
    SHELL_PRINTF("XO    : stat=%lx tune-wait=%lu restarts=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_xo_stat,
                 (unsigned long)tiku_radio_arch_dbg_xo_wait,
                 (unsigned long)tiku_radio_arch_dbg_xo_restarts);
    SHELL_PRINTF("WINDOW: hw=%lu forced=%lu (forced!=0 => DPPI window dead)\n",
                 (unsigned long)tiku_radio_arch_dbg_win_hw,
                 (unsigned long)tiku_radio_arch_dbg_win_forced);
}

static void bleadv_scan(unsigned secs, const char *prefix)
{
    tiku_ble_adv_report_t reps[12];
    char addrstr[18];
    int n, i;

    if (prefix != (const char *)0) {
        SHELL_PRINTF("scanning 37/38/39 for %u s (filter '%s*')...\n",
                     secs, prefix);
    } else {
        SHELL_PRINTF("scanning 37/38/39 for %u s...\n", secs);
    }
    n = tiku_ble_adv_scan_filter(reps, 12u, (uint16_t)(secs * 1000u),
                                 prefix);
    if (n < 0) {
        SHELL_PRINTF(SH_RED "no broadcast radio\n" SH_RST);
        return;
    }
    for (i = 0; i < n; i++) {
        bleadv_fmt_addr(addrstr, reps[i].addr);
        SHELL_PRINTF("  %s  rssi=%d  type=%u  %s\n", addrstr,
                     (int)reps[i].rssi, (unsigned)reps[i].adv_type,
                     reps[i].name);
    }
    SHELL_PRINTF("%d device%s\n", n, (n == 1) ? "" : "s");
}

/* Multi-PHY airtime probe (R8.1): the iteration count of the polled TX
 * window scales with airtime, so the same PDU at 2M/S2/S8 must land near
 * 0.5x/3x/8x of the 1M count -- the modulator's word against physics. */
static void bleadv_phy(void)
{
    static const char *nm[4] = { "1m", "2m", "s8", "s2" };
    static const tiku_radio_arch_phy_t ph[4] = {
        TIKU_RADIO_PHY_1M, TIKU_RADIO_PHY_2M,
        TIKU_RADIO_PHY_CODED_S8, TIKU_RADIO_PHY_CODED_S2,
    };
    uint32_t it[4][3];
    uint32_t avg[4];
    int i;

    if (tiku_ble_adv_active()) {
        SHELL_PRINTF(SH_RED "stop the beacon first (bleadv off)\n" SH_RST);
        return;                 /* offloaded RADIO = NS alias, keep out    */
    }
    tiku_radio_arch_init();     /* idempotent; probe needs the link config */
    SHELL_PRINTF("PHY airtime probe (TX-window iters, ch37/38/39):\n");
    for (i = 0; i < 4; i++) {
        if (tiku_radio_arch_phy_tx_probe(ph[i], it[i]) != 0) {
            SHELL_PRINTF(SH_RED "  %s: burst never disabled\n" SH_RST,
                         nm[i]);
            return;
        }
        avg[i] = (it[i][0] + it[i][1] + it[i][2]) / 3u;
        SHELL_PRINTF("  %s: %lu %lu %lu\n", nm[i],
                     (unsigned long)it[i][0], (unsigned long)it[i][1],
                     (unsigned long)it[i][2]);
    }
    if (avg[0] == 0u) {
        SHELL_PRINTF(SH_RED "1m probe empty\n" SH_RST);
        return;
    }
    SHELL_PRINTF("ratios vs 1m (x100): 2m=%lu s8=%lu s2=%lu\n",
                 (unsigned long)((avg[1] * 100u) / avg[0]),
                 (unsigned long)((avg[2] * 100u) / avg[0]),
                 (unsigned long)((avg[3] * 100u) / avg[0]));
}

/* Extended advertising bring-up (R8.3a): ADV_EXT_IND + hardware-timed
 * AUX_ADV_IND at ~100 ms intervals for ~secs.  The AdvData is
 * deliberately >31 bytes (Flags + name + a 47-byte 'TK' payload) --
 * the whole point of extended advertising.  Blocking with watchdog
 * kicks; the on-die oracle is dbg_aux_us: the aux packet's captured
 * start time must sit on the 600 us AuxPtr offset. */
static void bleadv_ext(const char *name, unsigned secs)
{
    static const char blob[] =
        "EXTENDED-ADV-PAYLOAD-BEYOND-31-BYTES-0123456789";
    uint8_t ad[80], addr[6];
    uint8_t adlen = 0u, nlen, plen = (uint8_t)(sizeof(blob) - 1u);
    unsigned long bursts = 0u, aux_last = 0ul;
    int rc = 0;
    tiku_clock_time_t deadline;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_radio_arch_init();
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;                       /* random static address       */

    nlen = (uint8_t)strlen(name);
    if (nlen > 20u) { nlen = 20u; }
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + nlen);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], name, nlen); adlen = (uint8_t)(adlen + nlen);
    ad[adlen++] = (uint8_t)(3u + plen);
    ad[adlen++] = 0xFFu; ad[adlen++] = 'T'; ad[adlen++] = 'K';
    memcpy(&ad[adlen], blob, plen); adlen = (uint8_t)(adlen + plen);

    SHELL_PRINTF("ext-adv '%s': %u-byte AdvData (legacy cap is 31),"
                 " ch37 -> aux ch20 @600us, ~%u s...\n",
                 name, (unsigned)adlen, secs);
    deadline = (tiku_clock_time_t)(tiku_clock_time() +
               (tiku_clock_time_t)secs * TIKU_CLOCK_SECOND);
    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        tiku_clock_time_t next;
        rc = tiku_radio_arch_extadv_burst(addr, ad, adlen);
        bursts++;
        aux_last = tiku_radio_arch_dbg_aux_us;
        if (rc != 0) {
            break;
        }
        next = (tiku_clock_time_t)(tiku_clock_time() +
                (TIKU_CLOCK_SECOND / 8u));      /* ~125 ms cadence         */
        while (TIKU_CLOCK_LT(tiku_clock_time(), next)) {
            tiku_watchdog_kick();
            tiku_nordic_wfe();
        }
    }
    SHELL_PRINTF("ext done: %lu bursts rc=%d aux=%luus (target 600)\n",
                 bursts, rc, aux_last);
}

/* Auto-stop for the `bleadv <name> [secs]` demo form: a one-shot callback
 * timer; both it and the beacon's burst timer dispatch cooperatively while
 * the shell idles at the prompt. */
static struct tiku_timer bleadv_stop_timer;

static void bleadv_autostop_cb(void *ptr)
{
    (void)ptr;
    SHELL_PRINTF("\nbleadv: beacon done (%lu bursts total)\n",
                 (unsigned long)tiku_ble_adv_bursts());
    tiku_ble_adv_stop();
}

void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[])
{
    const char *name;
    unsigned secs = 3u;

    if (argc < 2) {
        SHELL_PRINTF("usage: bleadv <name> [secs] | on <name> [ms] | off"
                     " | scan [secs] [prefix] | observe [secs|off]"
                     " | ext <name> [secs] | phy | dbg\n");
        return;
    }
    if (strcmp(argv[1], "dbg") == 0) {
        bleadv_dbg();
        return;
    }
    if (strcmp(argv[1], "phy") == 0) {
        bleadv_phy();
        return;
    }
    if (strcmp(argv[1], "ext") == 0) {
        unsigned s = 5u;
        if (argc < 3) {
            SHELL_PRINTF("usage: bleadv ext <name> [secs]\n");
            return;
        }
        if (argc >= 4) {
            long v = strtol(argv[3], (char **)0, 10);
            if (v > 0 && v <= 120) { s = (unsigned)v; }
        }
        bleadv_ext(argv[2], s);
        return;
    }
    if (strcmp(argv[1], "observe") == 0) {
        if (argc >= 3 && strcmp(argv[2], "off") == 0) {
            tiku_ble_adv_observe_stop();
            SHELL_PRINTF("observer off (%u device%s in /sys/radio/scan)\n",
                         (unsigned)tiku_ble_adv_last_scan_count(),
                         (tiku_ble_adv_last_scan_count() == 1u) ? "" : "s");
            return;
        }
        {
            unsigned long s = 0ul;              /* 0 = until observe off */
            if (argc >= 3) {
                s = strtoul(argv[2], (char **)0, 10);
                if (s > 3600ul) { s = 3600ul; }
            }
            if (tiku_ble_adv_observe_start((uint16_t)s) != 0) {
                SHELL_PRINTF(SH_RED "radio busy (%s) -- bleadv off first\n"
                             SH_RST, tiku_ble_adv_owner_str());
                return;
            }
            if (s != 0ul) {
                SHELL_PRINTF("observing in the background for %lu s"
                             " (cat /sys/radio/scan)\n", s);
            } else {
                SHELL_PRINTF("observing in the background"
                             " (cat /sys/radio/scan; bleadv observe off)\n");
            }
        }
        return;
    }
    if (strcmp(argv[1], "off") == 0) {
        tiku_ble_adv_stop();
        SHELL_PRINTF("beacon off\n");
        return;
    }
    if (strcmp(argv[1], "scan") == 0) {
        /* Positional-free tail: a numeric arg is the duration, anything
         * else is the name-prefix filter (order-independent). */
        unsigned s = 4u;
        const char *pfx = (const char *)0;
        uint8_t a;
        for (a = 2u; a < argc; a++) {
            char *end;
            long v = strtol(argv[a], &end, 10);
            if (end != argv[a] && *end == '\0') {
                if (v > 0 && v <= 30) { s = (unsigned)v; }
            } else if (pfx == (const char *)0) {
                pfx = argv[a];
            }
        }
        bleadv_scan(s, pfx);
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        unsigned long ms = 0ul;
        if (argc < 3) {
            SHELL_PRINTF("usage: bleadv on <name> [interval_ms]\n");
            return;
        }
        if (argc >= 4) {
            ms = strtoul(argv[3], (char **)0, 10);
        }
        if (tiku_ble_adv_beacon(argv[2], (uint16_t)ms) != 0) {
            SHELL_PRINTF(SH_RED "beacon start failed\n" SH_RST);
            return;
        }
        SHELL_PRINTF("beaconing '%s' every %u ms in the background"
                     " (bleadv off to stop)\n",
                     tiku_ble_adv_name(),
                     (unsigned)tiku_ble_adv_interval_ms());
        return;
    }

    /* Demo form: fast (100 ms) background beacon that stops itself after
     * ~secs.  The command returns immediately -- burst and auto-stop are
     * timer callbacks, dispatched while the shell idles at the prompt (a
     * blocking wait here would starve them: cooperative scheduling). */
    name = argv[1];
    if (argc >= 3) {
        long v = strtol(argv[2], (char **)0, 10);
        if (v > 0 && v <= 120) { secs = (unsigned)v; }
    }
    if (tiku_ble_adv_beacon(name, 100u) != 0) {
        SHELL_PRINTF(SH_RED "beacon start failed\n" SH_RST);
        return;
    }
    tiku_timer_set_callback(&bleadv_stop_timer,
                            (tiku_clock_time_t)secs * TIKU_CLOCK_SECOND,
                            bleadv_autostop_cb, (void *)0);
    SHELL_PRINTF("beaconing '%s' on 37/38/39 for ~%u s (background)...\n",
                 tiku_ble_adv_name(), secs);
}

#endif /* TIKU_SHELL_CMD_BLEADV */
