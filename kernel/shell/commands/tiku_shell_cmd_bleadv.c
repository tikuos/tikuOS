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
 *   bleadv scan [secs]     passive scan; lists addr/rssi/type/name
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
}

static void bleadv_scan(unsigned secs)
{
    tiku_ble_adv_report_t reps[12];
    char addrstr[18];
    int n, i;

    SHELL_PRINTF("scanning 37/38/39 for %u s...\n", secs);
    n = tiku_ble_adv_scan(reps, 12u, (uint16_t)(secs * 1000u));
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
                     " | scan [secs] | dbg\n");
        return;
    }
    if (strcmp(argv[1], "dbg") == 0) {
        bleadv_dbg();
        return;
    }
    if (strcmp(argv[1], "off") == 0) {
        tiku_ble_adv_stop();
        SHELL_PRINTF("beacon off\n");
        return;
    }
    if (strcmp(argv[1], "scan") == 0) {
        unsigned s = 4u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 30) { s = (unsigned)v; }
        }
        bleadv_scan(s);
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
