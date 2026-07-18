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
 *   bleadv conn [secs]     L3: advertise connectably, accept a central,
 *                          and HOLD the link (empty-PDU keepalive, CSA#1
 *                          hopping, SN/NESN, supervision).  Blocking;
 *                          connect from nRF Connect.  Reports events/held-ms
 *   bleadv connprobe [secs]
 *                          L1/L2: connectable ADV_IND, hardware-T_IFS
 *                          SCAN_RSP to scanners, and capture+decode a
 *                          central's CONNECT_IND LLData
 *   bleadv csa1            L3 self-test: CSA#1 hops vs an independent ref
 *   bleadv ackfsm          L3 self-test: SN/NESN ack window incl. resend
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
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>      /* L6: FLPR-as-controller (F-L6.1) */
#include <interfaces/bluetooth/tiku_ble_serial.h> /* facade (B3 auto-reconnect) */
#include <interfaces/bluetooth/tiku_ble_host.h>   /* Phase B: M33 ATT/GATT host */
#include <interfaces/bluetooth/tiku_ble_smp.h>     /* Phase E: SMP LESC crypto  */
#endif
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

/* SHELL_PRINTF has no %02X: format @p n bytes of @p b as hex into @p out.
 * @p msb_first prints b[n-1]..b[0] (display order for little-endian fields
 * like the access address); else b[0]..b[n-1]. */
static void bleadv_fmt_hex(char *out, const uint8_t *b, int n, int msb_first)
{
    static const char hex[] = "0123456789ABCDEF";
    int i, o = 0;
    for (i = 0; i < n; i++) {
        uint8_t v = msb_first ? b[n - 1 - i] : b[i];
        out[o++] = hex[v >> 4];
        out[o++] = hex[v & 0x0Fu];
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

/* Map a PHY name to the enum (default 1M). */
static tiku_radio_arch_phy_t bleadv_phy_of(const char *s)
{
    if (s != (const char *)0) {
        if (strcmp(s, "2m") == 0) {
            return TIKU_RADIO_PHY_2M;
        }
        if (strcmp(s, "s8") == 0) {
            return TIKU_RADIO_PHY_CODED_S8;
        }
        if (strcmp(s, "s2") == 0) {
            return TIKU_RADIO_PHY_CODED_S2;
        }
    }
    return TIKU_RADIO_PHY_1M;
}

/* R8.2 board<->board PER: TX N tagged PDUs at a PHY on ch37, paced so the
 * peer's single-shot RX can re-arm and catch each on a quiet channel. */
static void bleadv_phytx(uint8_t argc, const char *argv[])
{
    const char *pn = (argc > 2u) ? argv[2] : "1m";
    tiku_radio_arch_phy_t phy = bleadv_phy_of(pn);
    long n = (argc > 3u) ? (long)strtoul(argv[3], (char **)0, 10) : 100;
    uint8_t pdu[8];
    uint16_t i;
    uint32_t sent = 0u;

    if (n <= 0 || n > 5000) {
        n = 100;
    }
    if (tiku_ble_adv_active()) {
        SHELL_PRINTF(SH_RED "stop the beacon first (bleadv off)\n" SH_RST);
        return;
    }
    tiku_radio_arch_init();
    pdu[0] = 0x42u;                              /* S0: ADV_NONCONN_IND head   */
    pdu[1] = 4u;                                 /* LEN: 4-byte payload        */
    pdu[3] = 'P'; pdu[4] = 'H'; pdu[5] = 'Y';    /* magic tag                  */
    SHELL_PRINTF("PHY TX %s ch37: %ld pkts...\n", pn, n);
    tiku_radio_arch_constlat_hold(1);            /* erratum 20 across the run  */
    for (i = 0u; i < (uint16_t)n; i++) {
        volatile uint32_t d;
        pdu[6] = (uint8_t)i;                     /* seq                        */
        pdu[2] = pdu[3];                         /* S1 dup (erratum 49)        */
        if (tiku_radio_arch_phy_tx(phy, 0u, pdu) == 0) {
            sent++;
        }
        /* Modest inter-packet gap; the peer's RX stays armed in-place, so
         * this only has to clear the coded-PHY airtime + START re-arm. */
        for (d = 0u; d < 300000u; d++) {
        }
        if ((i & 0x1Fu) == 0u) {
            tiku_watchdog_kick();
        }
    }
    tiku_radio_arch_constlat_hold(0);
    tiku_radio_arch_init();                      /* restore 1M beacon/scan     */
    SHELL_PRINTF("PHY TX %s done: %lu sent\n", pn, (unsigned long)sent);
}

/* R8.2 RX half: count tagged CRC-OK packets heard at a PHY over ~secs. */
static void bleadv_phyrx(uint8_t argc, const char *argv[])
{
    static const uint8_t tag[3] = { 'P', 'H', 'Y' };
    const char *pn = (argc > 2u) ? argv[2] : "1m";
    tiku_radio_arch_phy_t phy = bleadv_phy_of(pn);
    long secs = (argc > 3u) ? (long)strtoul(argv[3], (char **)0, 10) : 6;
    int8_t last_rssi = 0;
    int ours;

    if (secs <= 0 || secs > 60) {
        secs = 6;
    }
    if (tiku_ble_adv_active()) {
        SHELL_PRINTF(SH_RED "stop the beacon first (bleadv off)\n" SH_RST);
        return;
    }
    tiku_radio_arch_init();
    SHELL_PRINTF("PHY RX %s ch37 ~%ld s (tag PHY)...\n", pn, secs);
    tiku_radio_arch_constlat_hold(1);
    ours = tiku_radio_arch_phy_rx_count(phy, 0u, (uint32_t)secs * 1000u,
                                        tag, 3u, 3u, &last_rssi);
    tiku_radio_arch_constlat_hold(0);
    tiku_radio_arch_init();
    SHELL_PRINTF("PHY RX %s done: %d ours rssi=%d\n", pn, ours, (int)last_rssi);
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

/* L1 bring-up: connectable advertising until a central sends us a
 * CONNECT_IND, then decode its LLData -- the first packet of the
 * link-layer ladder, captured off the air from a real central
 * (`bluetoothctl connect <addr>` on the host).  We do not accept the
 * connection yet; the central will retry and time out. */
static void bleadv_connprobe(unsigned secs)
{
    uint8_t ad[31], addr[6], lldata[22];
    uint8_t adlen = 0u;
    char addrstr[18];
    static const char nm[] = "TIKU-CONN";

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_radio_arch_init();
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    bleadv_fmt_addr(addrstr, addr);

    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + sizeof(nm) - 1u);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], nm, sizeof(nm) - 1u);
    adlen = (uint8_t)(adlen + sizeof(nm) - 1u);

    SHELL_PRINTF("ADV_IND as %s '%s'; connect from a central within %u s"
                 " (bluetoothctl: connect %s)\n", addrstr, nm, secs,
                 addrstr);
    if (tiku_radio_arch_connadv_probe(addr, ad, adlen, lldata,
                                      secs * 1000u) == 1) {
        /* LLData layout: AA(4) CRCInit(3) WinSize(1) WinOffset(2)
         * Interval(2) Latency(2) Timeout(2) ChM(5) Hop:5|SCA:3(1).
         * SHELL_PRINTF has no %X -> format the multi-byte fields by
         * hand (bleadv_fmt_hex), and keep hex + decimal in SEPARATE
         * printf calls so a hex field can never poison a %u vararg. */
        char aa[9], crc[7], chm[11];
        unsigned interval = (unsigned)lldata[10] |
                            ((unsigned)lldata[11] << 8);
        bleadv_fmt_hex(aa, &lldata[0], 4, 1);      /* AA, display MSB-first */
        bleadv_fmt_hex(crc, &lldata[4], 3, 1);     /* CRCInit               */
        bleadv_fmt_hex(chm, &lldata[16], 5, 0);    /* ChM, ch0 = LSB byte0  */
        SHELL_PRINTF(SH_GREEN "CONNECT_IND captured!\n" SH_RST);
        SHELL_PRINTF("  AA=%s crcinit=%s\n", aa, crc);
        SHELL_PRINTF("  winsize=%u winoff=%u\n",
                     (unsigned)lldata[7],
                     (unsigned)lldata[8] | ((unsigned)lldata[9] << 8));
        SHELL_PRINTF("  interval=%u (%u.%02u ms) latency=%u timeout=%ums\n",
                     interval, (interval * 125u) / 100u,
                     (interval * 125u) % 100u,
                     (unsigned)lldata[12] | ((unsigned)lldata[13] << 8),
                     ((unsigned)lldata[14] |
                      ((unsigned)lldata[15] << 8)) * 10u);
        SHELL_PRINTF("  chmap=%s hop=%u sca=%u\n", chm,
                     (unsigned)(lldata[21] & 0x1Fu),
                     (unsigned)(lldata[21] >> 5));
    } else {
        SHELL_PRINTF("no CONNECT_IND in %u s\n", secs);
    }
    SHELL_PRINTF("  adv_tx=%lu scan_req=%lu scan_rsp=%lu tifs=%luus"
                 " rx_other=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_connadv_tx,
                 (unsigned long)tiku_radio_arch_dbg_connadv_scanreq,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rsp,
                 (unsigned long)tiku_radio_arch_dbg_connadv_tifs,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxother);
}

/* CSA#1 cross-check (L3 groundwork): expected sequences generated by an
 * INDEPENDENT Python implementation of Core 4.5.8.2 -- three maps, the
 * last two exercising the remap path hard. */
static void bleadv_csa1(void)
{
    static const struct {
        uint8_t map[5];
        uint8_t last, hop;
        uint8_t expect[8];
    } v[3] = {
        { { 0xFF, 0xFF, 0xFF, 0xFF, 0x1F }, 0, 7,
          { 7, 14, 21, 28, 35, 5, 12, 19 } },
        { { 0x00, 0xFE, 0xFF, 0xFF, 0x1F }, 3, 11,
          { 14, 25, 36, 10, 21, 32, 15, 17 } },
        { { 0x01, 0x10, 0x00, 0x01, 0x10 }, 0, 13,
          { 12, 24, 24, 36, 0, 0, 12, 24 } },
    };
    int i, s, fails = 0;

    for (i = 0; i < 3; i++) {
        uint8_t last = v[i].last;
        for (s = 0; s < 8; s++) {
            uint8_t un;
            uint8_t ch = tiku_radio_ll_csa1_next(last, v[i].hop,
                                                 v[i].map, &un);
            if (ch != v[i].expect[s]) {
                SHELL_PRINTF(SH_RED "csa1: map%d step%d got %u want %u\n"
                             SH_RST, i, s, (unsigned)ch,
                             (unsigned)v[i].expect[s]);
                fails++;
            }
            last = un;                  /* advance by UNMAPPED, not ch  */
        }
    }
    if (fails == 0) {
        SHELL_PRINTF(SH_GREEN "csa1: 24/24 hops match the independent"
                     " reference\n" SH_RST);
    }
}

/* L3 two-board harness: this board is the CENTRAL -- scan for TIKU-CONN,
 * connect, drive the link.  Run `bleadv conn` on the peer (peripheral).
 * We impose lenient params so the link establishes while timing is tuned;
 * reports events, peripheral responses heard, and the measured peripheral
 * T_IFS (the ground truth the phone could never give). */
static void bleadv_central(unsigned secs, uint8_t updates)
{
    uint8_t addr[6];
    tiku_radio_ll_conn_stats_t st;
    char addrstr[18];
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    bleadv_fmt_addr(addrstr, addr);
    tiku_radio_arch_central_updates(updates);     /* Phase A: arm LL updates */
    SHELL_PRINTF("CENTRAL %s: scanning for TIKU-CONN, up to %u s%s...\n",
                 addrstr, secs,
                 updates ? " (will send CHANNEL_MAP + CONNECTION updates)"
                         : "");
    rc = tiku_radio_arch_central(addr, secs, &st);
    if (rc != 0) {
        SHELL_PRINTF("no TIKU-CONN peripheral found in %u s\n", secs);
        return;
    }
    SHELL_PRINTF(SH_GREEN "connection ran %lu ms\n" SH_RST,
                 (unsigned long)st.ms);
    SHELL_PRINTF("  events=%lu rx_ok=%lu addr_only=%lu missed=%lu"
                 "  (%lu%% responded)\n",
                 (unsigned long)st.events, (unsigned long)st.rx_ok,
                 (unsigned long)st.addr_seen, (unsigned long)st.missed,
                 st.events ? (unsigned long)(st.rx_ok * 100u / st.events)
                           : 0ul);
    SHELL_PRINTF("  peripheral T_IFS=%lu us (spec 150)\n",
                 (unsigned long)tiku_radio_arch_dbg_cen_tifs);
    SHELL_PRINTF("  LL ctrl: tx=%lu rx=%lu peer_version=%u (L4)\n",
                 (unsigned long)st.ctrl_tx, (unsigned long)st.ctrl_rx,
                 (unsigned)st.peer_vers);
    {
        char rb[3];
        bleadv_fmt_hex(rb, &st.att_readback, 1, 0);
        if (st.att_step >= 8u && st.att_ok) {
            SHELL_PRINTF(SH_GREEN "  NUS: MTU/discover/CCCD/write->notify"
                         " loopback OK, echo[0]=0x%s (L5/L6)\n" SH_RST, rb);
        } else {
            SHELL_PRINTF("  NUS: incomplete (step=%u/8 echo[0]=0x%s) (L5)\n",
                         (unsigned)st.att_step, rb);
        }
        SHELL_PRINTF("  GATT discovery: %s (handles RX/TX/CCCD matched)"
                     " (L6)\n", st.att_disc ? "OK" : "not matched");
    }
    if (st.att_lread || st.att_lwrite) {
        SHELL_PRINTF("  GATT long ops: read-blob=%s prep/exec-write=%s"
                     " (table-driven DB, Phase D)\n",
                     st.att_lread ? "OK" : "FAIL",
                     st.att_lwrite ? "OK" : "FAIL");
    }
    SHELL_PRINTF("  ended: %s\n",
                 st.reason == 0u ? "duration cap" :
                 st.reason == 1u ? "supervision (peripheral silent)" :
                 st.reason == 3u ? "peer terminated" :
                                   "never connected");
}

/* L3: advertise connectably, accept a central, HOLD the link.  Blocking
 * (parks the shell while connected); a real central (nRF Connect on a
 * phone) is the oracle.  Exit: link held for many events / minutes. */
static void bleadv_conn(unsigned secs)
{
    uint8_t ad[31], addr[6];
    uint8_t adlen = 0u;
    char addrstr[18];
    tiku_radio_ll_conn_stats_t st;
    static const char nm[] = "TIKU-CONN";
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    bleadv_fmt_addr(addrstr, addr);

    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + sizeof(nm) - 1u);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], nm, sizeof(nm) - 1u);
    adlen = (uint8_t)(adlen + sizeof(nm) - 1u);

    SHELL_PRINTF("ADV_IND as %s '%s'; CONNECT from a central (nRF Connect),"
                 " up to %u s...\n", addrstr, nm, secs);
    rc = tiku_radio_arch_connect(addr, ad, adlen, secs, &st);
    if (rc != 0) {
        SHELL_PRINTF("no central connected in %u s\n", secs);
        return;
    }
    SHELL_PRINTF(SH_GREEN "connection held %lu ms\n" SH_RST,
                 (unsigned long)st.ms);
    SHELL_PRINTF("  events=%lu rx_ok=%lu addr_only=%lu missed=%lu"
                 "  (%lu%% received)\n",
                 (unsigned long)st.events, (unsigned long)st.rx_ok,
                 (unsigned long)st.addr_seen, (unsigned long)st.missed,
                 st.events ? (unsigned long)(st.rx_ok * 100u / st.events)
                           : 0ul);
    SHELL_PRINTF("  hop=%u first_chan=%u interval=%u winsize=%u winoff=%u\n",
                 (unsigned)st.hop, (unsigned)st.first_chan,
                 (unsigned)st.interval, (unsigned)st.winsize,
                 (unsigned)st.winoff);
    SHELL_PRINTF("  first_anchor_delta=%ld us (actual - predicted)\n",
                 (long)st.first_delta);
    SHELL_PRINTF("  LL ctrl: tx=%lu rx=%lu peer_version=%u (L4)\n",
                 (unsigned long)st.ctrl_tx, (unsigned long)st.ctrl_rx,
                 (unsigned)st.peer_vers);
    if (st.att_readback != 0u) {
        char wv[3];
        bleadv_fmt_hex(wv, &st.att_readback, 1, 0);
        SHELL_PRINTF("  NUS server: last RX write[0]=0x%s at handle 0x0012 (L5)"
                     "\n", wv);
    }
    {
        char fb[11];
        bleadv_fmt_hex(fb, st.fail_bytes, 5, 0);
        SHELL_PRINTF("  crc_fail_bytes=%s (S0 LEN S1 pay0 pay1)\n", fb);
    }
    SHELL_PRINTF("  ended: %s\n",
                 st.reason == 0u ? "duration cap" :
                 st.reason == 1u ? "supervision timeout (central left)" :
                 st.reason == 3u ? "peer terminated" :
                                   "never connected");
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

#if (TIKU_FLPR_ENABLE + 0)
/* L6 F-L6.1 step 0: prove the FLPR can drive RADIO RX (the foundational
 * new primitive for the FLPR-as-BLE-controller).  Blocking probe; drive a
 * transmitter on the peer board first, e.g. `bleadv beacon "TK",20`. */
static void bleadv_flprrx(void)
{
    uint32_t addr_evts = 0u, crcok = 0u, flen = 0u;
    uint8_t  first[16];
    int      rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    /* Boot the coprocessor for real: alive() can read a STALE magic left in
     * SRAM by a prior run across a warm reset, so start() (which scrubs the
     * shared page and re-boots) is the reliable gate, not alive(). */
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR not running (build with TIKU_FLPR_ENABLE=1)\n");
        return;
    }
    SHELL_PRINTF("FLPR RX probe: listening on ch37 ~4-5 s -- transmit adv on"
                 " the peer (e.g. 'bleadv beacon \"TK\",20')...\n");
    tiku_radio_arch_init();                 /* static link cfg (radio secure) */
    tiku_radio_arch_constlat_hold(1);       /* erratum-20 across the listen   */
    rc = tiku_flpr_arch_rxprobe(&addr_evts, &crcok, first, sizeof(first),
                                &flen);
    tiku_radio_arch_constlat_hold(0);
    if (rc != 0) {
        SHELL_PRINTF("FLPR RX probe failed (rc=%d)\n", rc);
        return;
    }
    SHELL_PRINTF("  addr_matches=%lu crc_ok=%lu\n",
                 (unsigned long)addr_evts, (unsigned long)crcok);
    if (flen != 0u) {
        char hx[40];
        bleadv_fmt_hex(hx, first, (int)(flen > 16u ? 16u : flen), 0);
        SHELL_PRINTF("  first CRC-ok pkt: %s (S0 LEN S1 ...)\n", hx);
    }
    if (crcok != 0u) {
        SHELL_PRINTF(SH_GREEN "  FLPR RADIO RX WORKS (L6 F-L6.1 step 0)\n"
                     SH_RST);
    } else if (addr_evts != 0u) {
        SHELL_PRINTF("  AA matched but 0 CRC-ok (whitening/format?)\n");
    } else {
        SHELL_PRINTF("  nothing heard (is the peer transmitting on ch37?)\n");
    }
}

/* L6 F-L6.1 step 1a: the FLPR advertises 'TIKU-CONN' connectably and
 * captures the CONNECT_IND -- the on-die controller taking its first
 * connection.  Connect from a central (`bleadv central` on the peer). */
static void bleadv_flpradv(void)
{
    uint8_t addr[6], ad[31], adv[48];
    uint8_t adlen = 0u, advlen;
    static const char nm[] = "TIKU-CONN";
    tiku_flpr_conn_info_t info;
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR not running (build with TIKU_FLPR_ENABLE=1)\n");
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + sizeof(nm) - 1u);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], nm, sizeof(nm) - 1u);
    adlen = (uint8_t)(adlen + sizeof(nm) - 1u);
    advlen = tiku_radio_arch_adv_build(adv, addr, ad, adlen);
    adv[0] = 0x40u;                              /* ADV_IND (connectable)    */

    SHELL_PRINTF("FLPR advertising 'TIKU-CONN' (connectable) ~8 s -- connect"
                 " from a central (bleadv central on the peer)...\n");
    tiku_radio_arch_init();
    NRF_RADIO_S->TIFS = 150u;                    /* T_IFS turnaround (hold)  */
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_capture(adv, advlen, addr, &info);
    if (rc == -1) {
        tiku_radio_arch_constlat_hold(0);
        SHELL_PRINTF("FLPR not running\n");
        return;
    }
    if (rc == -2) {
        tiku_radio_arch_constlat_hold(0);
        SHELL_PRINTF("no central connected (FLPR gave up advertising)\n");
        return;
    }
    {
        uint8_t aab[4], cib[3];
        char aa[9], ci[7];
        aab[0] = (uint8_t)(info.aa >> 24); aab[1] = (uint8_t)(info.aa >> 16);
        aab[2] = (uint8_t)(info.aa >> 8);  aab[3] = (uint8_t)info.aa;
        cib[0] = (uint8_t)(info.crcinit >> 16);
        cib[1] = (uint8_t)(info.crcinit >> 8); cib[2] = (uint8_t)info.crcinit;
        bleadv_fmt_hex(aa, aab, 4, 0);
        bleadv_fmt_hex(ci, cib, 3, 0);
        SHELL_PRINTF(SH_GREEN "  FLPR captured CONNECT_IND: AA=%s CRCInit=%s"
                     " interval=%u hop=%u timeout=%u (L6 F-L6.1 step 1a)\n"
                     SH_RST, aa, ci, (unsigned)info.interval,
                     (unsigned)info.hop, (unsigned)info.timeout);
    }
    /* Step 1b: the FLPR now HOLDS the link autonomously.  Watch conn_events
     * rise for ~10 s -- the M33 is just polling a shared word while the
     * coprocessor keeps the connection alive on the other core. */
    SHELL_PRINTF("  FLPR holding link autonomously (M33 only reads state)...\n");
    {
        unsigned t;
        for (t = 1u; t <= 10u; t++) {
            tiku_clock_time_t t0 = tiku_clock_time();
            while ((tiku_clock_time_t)(tiku_clock_time() - t0) <
                   (tiku_clock_time_t)TIKU_CLOCK_SECOND) {
                tiku_watchdog_kick();
            }
            SHELL_PRINTF("  t=%2us events=%lu %s\n", t,
                         (unsigned long)tiku_flpr_arch_conn_events(),
                         tiku_flpr_arch_conn_active() ? "HELD" : "DROPPED");
            if (!tiku_flpr_arch_conn_active()) {
                break;
            }
        }
    }
    tiku_flpr_arch_conn_stop();
    tiku_radio_arch_constlat_hold(0);
    SHELL_PRINTF("  stopped (FLPR serviced %lu events, L6 F-L6.1 step 1b)\n",
                 (unsigned long)tiku_flpr_arch_conn_events());
}

/* L6 F-L6.2: the FLPR carries NUS DATA.  Connect, then echo bytes the
 * central writes (NUS RX -> f2a mailbox) straight back as notifications
 * (a2f mailbox -> NUS TX) -- the loopback runs central <-> FLPR <-> M33,
 * exercising the tiku_ble_serial recv/send primitives end to end. */
/* Drain the host's pending TX PDU as data-PDU-sized fragments, each tagged
 * with its LLID (2 start / 1 continuation), flow-controlled by the mailbox. */
/* Phase E foundation: SMP LESC crypto self-test (AES-CMAC RFC-4493 KAT +
 * P-256 ECDH round-trip) -- the primitives pairing is built on. */
static void bleadv_smp(void)
{
    int r = tiku_ble_smp_selftest();
    SHELL_PRINTF("SMP LESC crypto self-test:\n");
    SHELL_PRINTF("  AES-CMAC (RFC 4493 KAT):   %s\n",
                 (r & 1) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  P-256 ECDH (round-trip):   %s\n",
                 (r & 2) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    if (r == 3) {
        SHELL_PRINTF(SH_GREEN "  crypto foundation OK (CMAC + ECDH) -- SMP"
                     " pairing buildable\n" SH_RST);
    }
}

static void bleadv_flpr_drain_tx(void)
{
    uint8_t  frag[32], llid;
    uint16_t fl;
    while ((fl = tiku_ble_host_next_tx(frag, sizeof(frag), &llid)) > 0u) {
        while (tiku_flpr_arch_conn_send(frag, fl, llid) == -2 &&
               tiku_flpr_arch_conn_active()) {
            tiku_watchdog_kick();
        }
        if (!tiku_flpr_arch_conn_active()) {
            break;
        }
    }
}

static void bleadv_flprnus(uint8_t req_cpu)
{
    uint8_t addr[6], ad[31], adv[48];
    uint8_t cpu_sent = 0u;
    uint8_t adlen = 0u, advlen;
    static const char nm[] = "TIKU-CONN";
    tiku_flpr_conn_info_t info;
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR not running (build with TIKU_FLPR_ENABLE=1)\n");
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + sizeof(nm) - 1u);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], nm, sizeof(nm) - 1u);
    adlen = (uint8_t)(adlen + sizeof(nm) - 1u);
    advlen = tiku_radio_arch_adv_build(adv, addr, ad, adlen);
    adv[0] = 0x40u;

    SHELL_PRINTF("FLPR NUS pipe: advertising 'TIKU-CONN' -- connect a NUS"
                 " client (bleadv central on the peer)...\n");
    tiku_radio_arch_init();
    NRF_RADIO_S->TIFS = 150u;
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_capture(adv, advlen, addr, &info);
    if (rc != 0) {
        tiku_radio_arch_constlat_hold(0);
        SHELL_PRINTF("no NUS client connected (rc=%d)\n", rc);
        return;
    }
    SHELL_PRINTF("  connected; M33 NUS host live (ATT on M33), echoing"
                 " RX->TX ~15 s...\n");
    {
        uint8_t frame[40], nus[TIKU_BLE_HOST_MTU];
        char hx[50];
        uint32_t total = 0u;
        tiku_clock_time_t start = tiku_clock_time();
        tiku_ble_host_reset();                    /* Phase B/C: M33 host      */
        while ((tiku_clock_time_t)(tiku_clock_time() - start) <
               (tiku_clock_time_t)(TIKU_CLOCK_SECOND * 15u)) {
            /* Pump: L2CAP fragment in -> recombine + ATT/GATT on the M33 ->
             * response fragmented out.  A (possibly multi-fragment) NUS RX
             * write surfaces bytes we echo back as a notification. */
            uint8_t llid_in;
            int n;
            bleadv_flpr_drain_tx();               /* flush pending TX first   */
            n = tiku_flpr_arch_conn_recv(frame, sizeof(frame), &llid_in);
            if (n > 0) {
                tiku_ble_host_rx(frame, (uint16_t)n, llid_in);
                bleadv_flpr_drain_tx();           /* send the ATT response    */
                {
                    uint16_t m = tiku_ble_host_nus_recv(nus, sizeof(nus));
                    if (m > 0u) {
                        total += m;
                        bleadv_fmt_hex(hx, nus, m > 16u ? 16 : (int)m, 0);
                        SHELL_PRINTF("  NUS RX %u B [%s] -> echoed to TX\n",
                                     (unsigned)m, hx);
                        (void)tiku_ble_host_nus_notify(nus, m);
                        bleadv_flpr_drain_tx();   /* send the notification    */
                    }
                }
            }
            /* Phase C: once subscribed, ask the central (L2CAP signalling) for
             * a longer interval.  The central obliges + issues an LL update
             * the FLPR follows (conn_cu below rises) -- peripheral-initiated
             * reparametrise, the bookend to Phase A. */
            if (req_cpu && !cpu_sent && tiku_ble_host_subscribed() &&
                tiku_ble_host_request_conn_param(48u, 48u, 0u, 400u) == 0) {
                cpu_sent = 1u;
                bleadv_flpr_drain_tx();
                SHELL_PRINTF("  -> L2CAP Conn Param Update Req"
                             " (interval 30->60 ms)\n");
            }
            tiku_watchdog_kick();
            if (!tiku_flpr_arch_conn_active()) {
                break;
            }
        }
        {
            uint32_t gap = 0u, rx = 0u;
            uint32_t duty = tiku_flpr_arch_conn_anchor(&gap, &rx);
            if (gap != 0u) {
                SHELL_PRINTF("  anchored-RX: RADIO off %lu of %lu loop-iters"
                             " per interval (~%lu%% RX duty, closed-loop)\n",
                             (unsigned long)gap,
                             (unsigned long)(gap + rx),
                             (unsigned long)duty);
            } else {
                SHELL_PRINTF("  anchored-RX: continuous (not yet converged)\n");
            }
        }
        {
            uint32_t cm = 0u, cu = 0u;
            uint32_t evt = tiku_flpr_arch_conn_events();
            (void)tiku_flpr_arch_conn_updates(&cm, &cu);
            /* events= is the survival proof: the FLPR only advances it while
             * it keeps catching the central; a mis-applied update desyncs and
             * it freezes near the Instant (~event 50).  cm/cu are what it
             * followed to their Instant. */
            SHELL_PRINTF("  events=%lu  LL-updates: channel-map=%lu"
                         " connection=%lu\n",
                         (unsigned long)evt, (unsigned long)cm,
                         (unsigned long)cu);
            if (req_cpu) {
                if (cu != 0u) {
                    SHELL_PRINTF(SH_GREEN "  Phase C OK: peripheral-requested"
                                 " reparametrise -- central obliged, FLPR"
                                 " followed the LL update\n" SH_RST);
                } else {
                    SHELL_PRINTF("  Conn Param Update: not followed (central"
                                 " declined, or no LL update seen)\n");
                }
            } else if (cm != 0u && cu != 0u) {
                SHELL_PRINTF(SH_GREEN "  Phase A OK: link survived a"
                             " mid-connection channel-map + interval change"
                             "\n" SH_RST);
            } else {
                SHELL_PRINTF("  LL updates: incomplete (central sent none, or"
                             " the peer is pre-Phase-A)\n");
            }
        }
        tiku_flpr_arch_conn_stop();
        tiku_radio_arch_constlat_hold(0);
        if (total != 0u) {
            SHELL_PRINTF(SH_GREEN "  NUS pipe OK: %lu bytes RX'd + echoed"
                         " through the FLPR (L6 F-L6.2)\n" SH_RST,
                         (unsigned long)total);
        } else {
            SHELL_PRINTF("  no NUS bytes received (client didn't write?)\n");
        }
    }
}

/* B3: drive the tiku_ble_serial FACADE (not the arch directly) as a
 * persistent NUS echo service for ~secs, so the auto-reconnect can be
 * exercised: connect a central, let it drop, and the service re-advertises
 * for the next one.  "link up (#N)" with N>=2 proves a reconnect. */
static void bleadv_serial(unsigned secs)
{
    tiku_clock_time_t start;
    uint32_t connects = 0u, echoed = 0u, last_st = 9u;
    uint8_t  was_ready = 0u, b[TIKU_BLE_HOST_MTU];  /* hold a recombined msg */

    if (tiku_ble_serial_start("TIKU-CONN") != 0) {
        SHELL_PRINTF("serial start failed (radio busy / FLPR down)\n");
        return;
    }
    SHELL_PRINTF("NUS serial 'TIKU-CONN' ~%u s (facade, auto-reconnect)...\n",
                 secs);
    start = tiku_clock_time();
    while ((tiku_clock_time_t)(tiku_clock_time() - start) <
           (tiku_clock_time_t)((uint32_t)TIKU_CLOCK_SECOND * secs)) {
        int r = tiku_ble_serial_ready();       /* also drives auto-reconnect  */
        uint32_t st = tiku_flpr_arch_conn_state();
        if (st != last_st) {
            SHELL_PRINTF("  [conn_state %lu]\n", (unsigned long)st);
            last_st = st;
        }
        if (r != 0 && was_ready == 0u) {
            connects++;
            SHELL_PRINTF("  link up (#%lu)\n", (unsigned long)connects);
        }
        was_ready = (uint8_t)(r != 0);
        if (r != 0 && tiku_ble_serial_rx_ready()) {
            int n = tiku_ble_serial_recv(b, sizeof(b));
            if (n > 0) {
                (void)tiku_ble_serial_send(b, (uint16_t)n);
                echoed++;
            }
        }
        tiku_watchdog_kick();
    }
    tiku_ble_serial_stop();
    SHELL_PRINTF("serial done: %lu link-ups, %lu echoed\n",
                 (unsigned long)connects, (unsigned long)echoed);
}
#endif /* TIKU_FLPR_ENABLE */

void tiku_shell_cmd_bleadv(uint8_t argc, const char *argv[])
{
    const char *name;
    unsigned secs = 3u;

    if (argc < 2) {
        SHELL_PRINTF("usage: bleadv <name> [secs] | on <name> [ms] | off"
                     " | scan [secs] [prefix] | observe [secs|off]"
                     " | conn [secs] | connprobe [secs] | csa1 | ackfsm"
                     " | ext <name> [secs] | phy | dbg\n");
        return;
    }
    if (strcmp(argv[1], "dbg") == 0) {
        bleadv_dbg();
        return;
    }
#if (TIKU_FLPR_ENABLE + 0)
    if (strcmp(argv[1], "flprrx") == 0) {
        bleadv_flprrx();
        return;
    }
    if (strcmp(argv[1], "flpradv") == 0) {
        bleadv_flpradv();
        return;
    }
    if (strcmp(argv[1], "smp") == 0) {            /* Phase E: SMP crypto test */
        bleadv_smp();
        return;
    }
    if (strcmp(argv[1], "flprnus") == 0) {
        bleadv_flprnus(0u);
        return;
    }
    if (strcmp(argv[1], "flprcpu") == 0) {        /* Phase C: request CP update*/
        bleadv_flprnus(1u);
        return;
    }
    if (strcmp(argv[1], "serial") == 0) {
        bleadv_serial(argc > 2 ? (unsigned)strtoul(argv[2], (char **)0, 10)
                               : 30u);
        return;
    }
#endif
    if (strcmp(argv[1], "phy") == 0) {
        bleadv_phy();
        return;
    }
    if (strcmp(argv[1], "phytx") == 0) {
        bleadv_phytx(argc, argv);
        return;
    }
    if (strcmp(argv[1], "phyrx") == 0) {
        bleadv_phyrx(argc, argv);
        return;
    }
    if (strcmp(argv[1], "csa1") == 0) {
        bleadv_csa1();
        return;
    }
    if (strcmp(argv[1], "ackfsm") == 0) {
        /* Scripted peer sequence incl. a retransmission (step 2): the
         * hand-computed expected (new,acked,sn,nesn) after each RX. */
        static const struct {
            uint8_t rx_sn, rx_nesn, newd, ackd, sn, nesn;
        } seq[4] = {
            { 0, 0, 1, 0, 0, 1 },   /* first packet: new, no ack yet     */
            { 1, 1, 1, 1, 1, 0 },   /* acks us + new data                */
            { 1, 1, 0, 0, 1, 0 },   /* peer RE-SENDS: not new, not acked */
            { 0, 0, 1, 1, 0, 1 },   /* acks us + new data                */
        };
        tiku_radio_ll_ack_t a = { 0u, 0u };
        int i, fails = 0;
        for (i = 0; i < 4; i++) {
            uint8_t r = tiku_radio_ll_ack(&a, seq[i].rx_sn, seq[i].rx_nesn,
                                          1u);  /* scripted seq = data PDUs */
            uint8_t nd = (r & TIKU_RADIO_LL_NEWDATA) ? 1u : 0u;
            uint8_t ak = (r & TIKU_RADIO_LL_ACKED) ? 1u : 0u;
            if (nd != seq[i].newd || ak != seq[i].ackd ||
                a.sn != seq[i].sn || a.nesn != seq[i].nesn) {
                SHELL_PRINTF(SH_RED "ackfsm: step%d new=%u ack=%u sn=%u"
                             " nesn=%u\n" SH_RST, i, nd, ak, a.sn, a.nesn);
                fails++;
            }
        }
        if (!fails) {
            SHELL_PRINTF(SH_GREEN "ackfsm: 4/4 incl. retransmission"
                         " (SN/NESN correct)\n" SH_RST);
        }
        return;
    }
    if (strcmp(argv[1], "central") == 0) {
        unsigned s = 60u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        bleadv_central(s, 0u);
        return;
    }
    if (strcmp(argv[1], "cenupd") == 0) {         /* Phase A: central + updates*/
        unsigned s = 30u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        bleadv_central(s, 1u);
        return;
    }
    if (strcmp(argv[1], "conn") == 0) {
        unsigned s = 60u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        bleadv_conn(s);
        return;
    }
    if (strcmp(argv[1], "connprobe") == 0) {
        unsigned s = 20u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 120) { s = (unsigned)v; }
        }
        bleadv_connprobe(s);
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
