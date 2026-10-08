/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_rftest.c - "rftest" command: the nRF54L radio lab.
 *
 * A test carrier, the PHY airtime and board-to-board PER probes, extended
 * advertising, the scan-request timer, the observer and its counters, the
 * link-layer self-tests and the FLPR's probes.  Opt-in, bench only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_rftest.h"
#include <kernel/cpu/tiku_common.h>

#include <stdlib.h>
#include <string.h>
#include <kernel/shell/tiku_shell_config.h>   /* resolved command flag  */
#include <kernel/shell/tiku_shell_io.h>

/* The config header forces this flag to 0 without TIKU_HAS_BLE_ADV, and
 * this file then compiles to nothing. */
#if TIKU_SHELL_CMD_RFTEST

#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND */
#include <arch/nordic/tiku_device_select.h>    /* NRF_* register blocks  */
#include <arch/nordic/tiku_nordic_core.h>      /* wfe (ext pacing)       */
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <interfaces/bluetooth/tiku_ble_adv.h> /* the radio's owner      */
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>        /* FLPR BLE controller    */
#endif

/* Band limits; the arch layer enforces the same range. */
#define RFT_MHZ_MIN   2360u
#define RFT_MHZ_MAX   2500u

/** Frequency the running carrier was started on (for "status"). */
static uint16_t rft_mhz;
/** Non-zero if the running carrier is the modulated form. */
static uint8_t  rft_modulated;

/** @brief Parse a non-negative decimal; -1 on NULL or a trailing non-digit. */
static long rft_atoi(const char *s)
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

/** @brief Parse an optionally negative decimal; @p dflt on NULL or error. */
static long rft_atoi_signed(const char *s, long dflt)
{
    if (s == (const char *)0) {
        return dflt;
    }
    if (*s == '-') {
        long v = rft_atoi(s + 1);
        return (v < 0) ? dflt : -v;
    }
    {
        long v = rft_atoi(s);
        return (v < 0) ? dflt : v;
    }
}

/** @brief Map 1m, 2m, s8 or s2 to the arch PHY and its name; 1M otherwise. */
static tiku_radio_arch_phy_t rft_phy(const char *arg, const char **name)
{
    *name = "1M";
    if (arg == (const char *)0) {
        return TIKU_RADIO_PHY_1M;
    }
    if (strcmp(arg, "2m") == 0 || strcmp(arg, "2M") == 0) {
        *name = "2M";
        return TIKU_RADIO_PHY_2M;
    }
    if (strcmp(arg, "s8") == 0 || strcmp(arg, "S8") == 0) {
        *name = "Coded S8";
        return TIKU_RADIO_PHY_CODED_S8;
    }
    if (strcmp(arg, "s2") == 0 || strcmp(arg, "S2") == 0) {
        *name = "Coded S2";
        return TIKU_RADIO_PHY_CODED_S2;
    }
    return TIKU_RADIO_PHY_1M;
}

/** @brief Apply a requested dBm, reporting when the silicon refuses it. */
static int rft_power(long dbm)
{
    if (dbm < -46 || dbm > 8) {
        SHELL_PRINTF("rftest: power %ld dBm out of range (-46..+8)\n", dbm);
        return -1;
    }
    if (tiku_radio_arch_set_txpower((int8_t)dbm) != 0) {
        SHELL_PRINTF("rftest: %ld dBm is not a silicon-legal step\n", dbm);
        return -1;
    }
    return 0;
}

/**
 * @brief Format a 6-byte address held in packet order (LSB first) as
 *        "AA:BB:CC:DD:EE:FF", MSB first; @p out needs 18 bytes.
 */
static void rft_fmt_addr(char *out, const uint8_t addr[6])
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

/**
 * @brief Format @p n bytes of @p b as hex into @p out (2n + 1 bytes).
 *
 * @p msb_first prints b[n-1]..b[0], the display order of a little-endian
 * field such as the access address; otherwise b[0]..b[n-1].
 */
static void rft_fmt_hex(char *out, const uint8_t *b, int n, int msb_first)
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

/**
 * @brief 1 if the SPU marks the RADIO secure, so that it answers on the
 *        secure alias; every path that lends the radio to the coprocessor
 *        makes it non-secure, whatever tiku_ble_adv_owner() reports.
 */
static uint8_t rft_radio_is_ours(void)
{
    return (NRF_SPU10_S->PERIPH[10].PERM & (1u << 4)) != 0u;
}

/** @brief 1 when the facade holds the radio; says so. */
static int rft_radio_busy(void)
{
    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s) -- write /sys/radio/beacon off,"
                     " or rftest observe off\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return 1;
    }
    return 0;
}

/** @brief Print the sub-command summary. */
static void rft_usage(void)
{
    SHELL_PRINTF("usage:\n");
    SHELL_PRINTF("  rftest cw <mhz> [dbm] [phy]    unmodulated carrier\n");
    SHELL_PRINTF("  rftest mod <mhz> [dbm] [phy]   modulated carrier\n");
    SHELL_PRINTF("  rftest sweep <lo> <hi> [dbm]   step across a range\n");
    SHELL_PRINTF("  rftest off                     stop transmitting\n");
    SHELL_PRINTF("  rftest status                  what is on air\n");
    SHELL_PRINTF("  rftest phy | phytx [phy] [n] [len] | phyrx [phy] [secs]\n");
    SHELL_PRINTF("  rftest ext <name> [secs] | scanreq [name] [secs] [ticks]\n");
    SHELL_PRINTF("  rftest scan [secs] [prefix] | observe [secs|off] | dbg\n");
    SHELL_PRINTF("  rftest csa1 | ackfsm"
#if (TIKU_FLPR_ENABLE + 0)
                 " | flprrx | flpradv"
#endif
                 "\n");
    SHELL_PRINTF("mhz %u..%u, dbm -46..+8 (default 0), "
                 "phy 1m|2m|s8|s2 (default 1m)\n",
                 RFT_MHZ_MIN, RFT_MHZ_MAX);
}

/** @brief Name a RADIO.STATE value, or "?" for one not listed. */
static const char *rft_state_name(uint32_t st)
{
    switch (st) {
    case 0x0u:  return "DISABLED";
    case 0x1u:  return "RXRU";
    case 0x2u:  return "RXIDLE";
    case 0x3u:  return "RX";
    case 0x9u:  return "TXRU";
    case 0xAu:  return "TXIDLE (carrier radiating)";
    case 0xBu:  return "TX (modulating)";
    case 0xCu:  return "TXDISABLE";
    default:    return "?";
    }
}

/** @brief Print whether a carrier is on air, and RADIO.STATE as read. */
static void rft_status(void)
{
    uint32_t st = tiku_radio_arch_state();

    if (tiku_radio_arch_carrier_active()) {
        SHELL_PRINTF("rftest: ON AIR %u MHz %s %d dBm "
                     "(radio busy until 'rftest off')\n",
                     rft_mhz, rft_modulated ? "modulated" : "unmodulated",
                     tiku_radio_arch_txpower());
    } else {
        SHELL_PRINTF("rftest: idle\n");
    }
    SHELL_PRINTF("rftest: RADIO.STATE = 0x%x %s\n",
                 (unsigned)st, rft_state_name(st));
}

/** @brief Start a carrier at @p mhz_s MHz, @p dbm_s dBm, on PHY @p phy_s. */
static void rft_start(const char *mhz_s, const char *dbm_s,
                      const char *phy_s, int modulated)
{
    const char *phy_name;
    tiku_radio_arch_phy_t phy;
    long mhz;

    mhz = rft_atoi(mhz_s);
    if (mhz < (long)RFT_MHZ_MIN || mhz > (long)RFT_MHZ_MAX) {
        SHELL_PRINTF("rftest: frequency must be %u..%u MHz\n",
                     RFT_MHZ_MIN, RFT_MHZ_MAX);
        return;
    }
    if (rft_power(rft_atoi_signed(dbm_s, 0)) != 0) {
        return;
    }
    phy = rft_phy(phy_s, &phy_name);

    if (tiku_radio_arch_carrier_start(phy, (uint16_t)mhz, modulated) != 0) {
        SHELL_PRINTF("rftest: could not start (ramp-up timeout)\n");
        return;
    }
    rft_mhz       = (uint16_t)mhz;
    rft_modulated = (uint8_t)(modulated != 0);

    SHELL_PRINTF("rftest: %s carrier ON at %ld MHz, %d dBm, %s\n",
                 modulated ? "modulated" : "unmodulated", mhz,
                 tiku_radio_arch_txpower(), phy_name);
    SHELL_PRINTF("rftest: radio held -- 'rftest off' to release it\n");
}

/**
 * @brief Step an unmodulated 1M-PHY carrier across [lo, hi] a MHz at a time.
 *
 * Dwells about 20 ms on each step, then stops the carrier; the call blocks
 * until the sweep ends.
 */
static void rft_sweep(const char *lo_s, const char *hi_s, const char *dbm_s)
{
    long lo = rft_atoi(lo_s);
    long hi = rft_atoi(hi_s);
    long f;

    if (lo < (long)RFT_MHZ_MIN || hi > (long)RFT_MHZ_MAX || lo > hi) {
        SHELL_PRINTF("rftest: bad range (need %u <= lo <= hi <= %u)\n",
                     RFT_MHZ_MIN, RFT_MHZ_MAX);
        return;
    }
    if (rft_power(rft_atoi_signed(dbm_s, 0)) != 0) {
        return;
    }

    SHELL_PRINTF("rftest: sweeping %ld..%ld MHz at %d dBm\n",
                 lo, hi, tiku_radio_arch_txpower());
    for (f = lo; f <= hi; f++) {
        if (tiku_radio_arch_carrier_start(TIKU_RADIO_PHY_1M,
                                          (uint16_t)f, 0) != 0) {
            SHELL_PRINTF("rftest: start failed at %ld MHz\n", f);
            break;
        }
        {
            unsigned ms;
            for (ms = 0u; ms < 20u; ms++) {
                tiku_common_delay_ms(1u);
                tiku_watchdog_kick();
            }
        }
        tiku_radio_arch_carrier_stop();
    }
    SHELL_PRINTF("rftest: sweep done, radio released\n");
}

/*---------------------------------------------------------------------------*/
/* PHY PROBES                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Single-board PHY airtime probe: one burst per PHY on 37/38/39.
 *
 * The polled TX window's iteration count scales with airtime: the same PDU
 * at 2M, S2 and S8 gives about 0.5x, 3x and 8x the 1M count.
 */
static void rft_phy_probe(void)
{
    static const char *nm[4] = { "1m", "2m", "s8", "s2" };
    static const tiku_radio_arch_phy_t ph[4] = {
        TIKU_RADIO_PHY_1M, TIKU_RADIO_PHY_2M,
        TIKU_RADIO_PHY_CODED_S8, TIKU_RADIO_PHY_CODED_S2,
    };
    uint32_t it[4][3];
    uint32_t avg[4];
    int i;

    if (rft_radio_busy()) {
        return;                 /* the beacon owns RADIO, non-secure on FLPR */
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

/**
 * @brief Board-to-board PER, transmit side: send N tagged PDUs at a PHY on
 *        ch37 for rftest phyrx on the peer to count.
 *
 * argv[2] is the PHY (default 1m), argv[3] the count (default 100, at most
 * 5000), argv[4] the payload length (4..36, default 4).
 */
static void rft_phytx(uint8_t argc, const char *argv[])
{
    const char *pn = (argc > 2u) ? argv[2] : "1m";
    const char *phy_name;
    tiku_radio_arch_phy_t phy = rft_phy(pn, &phy_name);
    long n = (argc > 3u) ? (long)strtoul(argv[3], (char **)0, 10) : 100;
    long plen = (argc > 4u) ? (long)strtoul(argv[4], (char **)0, 10) : 4;
    uint8_t pdu[40];
    uint16_t i;
    uint32_t sent = 0u;

    if (n <= 0 || n > 5000) {
        n = 100;
    }
    if (plen < 4 || plen > 36) {
        plen = 4;
    }
    if (rft_radio_busy()) {
        return;
    }
    tiku_radio_arch_init();
    pdu[0] = 0x42u;                              /* S0: ADV_NONCONN_IND head  */
    pdu[1] = (uint8_t)plen;                      /* LEN                       */
    pdu[3] = 'P'; pdu[4] = 'H'; pdu[5] = 'Y';    /* magic tag                 */
    for (i = 6u; i < (uint16_t)(3 + plen); i++) {
        pdu[i] = (uint8_t)(0xA5u ^ i);           /* deterministic filler      */
    }
    SHELL_PRINTF("PHY TX %s ch37: %ld pkts...\n", pn, n);
    tiku_radio_arch_constlat_hold(1);            /* erratum 20 across the run */
    for (i = 0u; i < (uint16_t)n; i++) {
        volatile uint32_t d;
        pdu[6] = (uint8_t)i;                     /* seq                       */
        pdu[2] = pdu[3];                         /* S1 dup (erratum 49)       */
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
    tiku_radio_arch_init();                      /* restore 1M beacon/scan    */
    SHELL_PRINTF("PHY TX %s done: %lu sent\n", pn, (unsigned long)sent);
}

/**
 * @brief Board-to-board PER, receive side: count the tagged CRC-OK packets
 *        heard at a PHY on ch37 over argv[3] seconds (default 6, at most 60).
 */
static void rft_phyrx(uint8_t argc, const char *argv[])
{
    static const uint8_t tag[3] = { 'P', 'H', 'Y' };
    const char *pn = (argc > 2u) ? argv[2] : "1m";
    const char *phy_name;
    tiku_radio_arch_phy_t phy = rft_phy(pn, &phy_name);
    long secs = (argc > 3u) ? (long)strtoul(argv[3], (char **)0, 10) : 6;
    int8_t last_rssi = 0;
    int ours;

    if (secs <= 0 || secs > 60) {
        secs = 6;
    }
    if (rft_radio_busy()) {
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

/*---------------------------------------------------------------------------*/
/* ADVERTISING PROBES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Extended advertising for ~@p secs: ADV_EXT_IND plus a hardware-timed
 *        AUX_ADV_IND about every 125 ms.
 *
 * The AdvData (Flags, name and a 47-byte 'TK' payload) exceeds the 31-byte
 * legacy limit.  Blocking; dbg_aux_us reports the aux packet's measured
 * start, which the AuxPtr announces at 600 us.
 */
static void rft_ext(const char *name, unsigned secs)
{
    static const char blob[] =
        "EXTENDED-ADV-PAYLOAD-BEYOND-31-BYTES-0123456789";
    uint8_t ad[80], addr[6];
    uint8_t adlen = 0u, nlen, plen = (uint8_t)(sizeof(blob) - 1u);
    unsigned long bursts = 0u, aux_last = 0ul;
    int rc = 0;
    tiku_clock_time_t deadline;

    if (rft_radio_busy()) {
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

/**
 * @brief Active scanner timing: send SCAN_REQs to the advertiser named
 *        argv[2] (default TIKU) for argv[3] seconds and time its replies.
 *
 * argv[4], when given, sets the ticks from the advert's end to the
 * request's TXEN (the T_IFS the scanner keeps).
 */
static void rft_scanreq(uint8_t argc, const char *argv[])
{
    uint8_t scana[6];
    unsigned s = 10u;
    const char *nm = (argc >= 3) ? argv[2] : "TIKU";

    if (argc >= 4) {
        long v = strtol(argv[3], (char **)0, 10);
        if (v > 0 && v <= 120) { s = (unsigned)v; }
    }
    if (argc >= 5) {                          /* ticks to request's TXEN  */
        long c = strtol(argv[4], (char **)0, 10);
        if (c >= 20 && c <= 2000) {
            tiku_radio_arch_scanreq_txen_ticks = (uint32_t)c;
        }
    }
    if (rft_radio_busy()) {
        return;
    }
    tiku_common_unique_id(scana, 6u);
    scana[5] |= 0xC0u;
    tiku_radio_arch_init();
    SHELL_PRINTF("scanning for '%s' and asking it for %u s...\n", nm, s);
    (void)tiku_radio_arch_scanreq_probe(scana, nm, s * 1000u);
    SHELL_PRINTF("  adv=%lu req=%lu rsp=%lu gap=%lu..%lu ticks, mean %lu"
                 " (a scanner's own request reads ~395)\n",
                 (unsigned long)tiku_radio_arch_dbg_scanreq_adv,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_sent,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_rsp,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_gap_min,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_gap_max,
                 (unsigned long)(tiku_radio_arch_dbg_scanreq_rsp ?
                    tiku_radio_arch_dbg_scanreq_gap_sum /
                    tiku_radio_arch_dbg_scanreq_rsp : 0u));
    SHELL_PRINTF("  windows: silent=%lu crc_bad=%lu other=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_scanreq_silent,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_crcbad,
                 (unsigned long)tiku_radio_arch_dbg_scanreq_wrong);
    {
        char hx[50];
        unsigned k;
        for (k = 0u; k < 3u && k < tiku_radio_arch_dbg_scanreq_wrong; k++) {
            rft_fmt_hex(hx, tiku_radio_arch_dbg_scanreq_pkt[k], 16, 0);
            SHELL_PRINTF("  heard: %s\n", hx);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* THE OBSERVER                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Scan 37/38/39 for @p secs and list up to 12 advertisers, only names
 *        starting with @p prefix when it is not NULL.
 */
static void rft_scan(unsigned secs, const char *prefix)
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
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    for (i = 0; i < n; i++) {
        rft_fmt_addr(addrstr, reps[i].addr);
        SHELL_PRINTF("  %s  rssi=%d  type=%u  %s\n", addrstr,
                     (int)reps[i].rssi, (unsigned)reps[i].adv_type,
                     reps[i].name);
    }
    SHELL_PRINTF("%d device%s\n", n, (n == 1) ? "" : "s");
}

/** @brief The background observer: start for argv[2] seconds (0 = until
 *         off), or `observe off`; /sys/radio/scan carries its table. */
static void rft_observe(uint8_t argc, const char *argv[])
{
    unsigned long s = 0ul;

    if (argc >= 3 && strcmp(argv[2], "off") == 0) {
        tiku_ble_adv_observe_stop();
        SHELL_PRINTF("observer off (%u device%s in /sys/radio/scan)\n",
                     (unsigned)tiku_ble_adv_last_scan_count(),
                     (tiku_ble_adv_last_scan_count() == 1u) ? "" : "s");
        return;
    }
    if (argc >= 3) {
        s = strtoul(argv[2], (char **)0, 10);
        if (s > 3600ul) { s = 3600ul; }
    }
    if (tiku_ble_adv_observe_start((uint16_t)s) != 0) {
        SHELL_PRINTF(SH_RED "radio busy (%s) -- free it first\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    if (s != 0ul) {
        SHELL_PRINTF("observing in the background for %lu s"
                     " (cat /sys/radio/scan)\n", s);
    } else {
        SHELL_PRINTF("observing in the background"
                     " (cat /sys/radio/scan; rftest observe off)\n");
    }
}

/**
 * @brief Print the FICR words that gate the errata workarounds (see
 *        tiku_crt_early.c), clock-tuning state and RADIO registers, then the
 *        tiku_ble_adv state and the burst, XO, scan and DPPI-window counters.
 */
static void rft_dbg(void)
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
    if (!rft_radio_is_ours()) {
        /* The radio is lent to the coprocessor (by tiku_ble_adv, the BLE
         * link or a session) and so non-secure: a read through the secure
         * alias is a bus fault. */
        SHELL_PRINTF("RADIO: non-secure (the coprocessor's, for %s); "
                     "registers not read\n", tiku_ble_adv_owner_str());
    } else {
        SHELL_PRINTF("RADIO: state=%lu mode=%lu txpower=%lx datawhite=%lx\n",
                     (unsigned long)NRF_RADIO_S->STATE,
                     (unsigned long)NRF_RADIO_S->MODE,
                     (unsigned long)NRF_RADIO_S->TXPOWER,
                     (unsigned long)NRF_RADIO_S->DATAWHITE);
    }
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
    {
        uint32_t isr = 0u, addr = 0u, crcok = 0u;
        uint32_t bad = 0u, kind = 0u, named = 0u, kept = 0u;

        tiku_radio_arch_scan_counts(&isr, &addr, &crcok);
        tiku_ble_adv_scan_drops(&bad, &kind, &named, &kept);
        SHELL_PRINTF("SCAN  : isr=%lu addr=%lu crcok=%lu\n",
                     (unsigned long)isr, (unsigned long)addr,
                     (unsigned long)crcok);
        SHELL_PRINTF("REPORT: kept=%lu drop ctx=%lu kind=%lu name=%lu\n",
                     (unsigned long)kept, (unsigned long)bad,
                     (unsigned long)kind, (unsigned long)named);
    }
    SHELL_PRINTF("WINDOW: hw=%lu forced=%lu (forced!=0 => DPPI window dead)\n",
                 (unsigned long)tiku_radio_arch_dbg_win_hw,
                 (unsigned long)tiku_radio_arch_dbg_win_forced);
}

/*---------------------------------------------------------------------------*/
/* LINK-LAYER SELF-TESTS                                                     */
/*---------------------------------------------------------------------------*/

/** @brief CSA#1 hops against an independent reference: three maps, eight
 *         steps each. */
static void rft_csa1(void)
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
            last = un;                  /* advance by unmapped, not ch  */
        }
    }
    if (fails == 0) {
        SHELL_PRINTF(SH_GREEN "csa1: 24/24 hops match the independent"
                     " reference\n" SH_RST);
    }
}

/**
 * @brief The SN/NESN window through a scripted peer sequence with a
 *        retransmission at step 2 and an empty PDU at step 4.
 *
 * An empty new packet advances NESN, delivering nothing.
 */
static void rft_ackfsm(void)
{
    static const struct {
        uint8_t rx_sn, rx_nesn, pay, newd, ackd, sn, nesn;
    } seq[5] = {
        { 0, 0, 1, 1, 0, 0, 1 },   /* first packet: new, no ack yet  */
        { 1, 1, 1, 1, 1, 1, 0 },   /* peer acks + sends new data     */
        { 1, 1, 1, 0, 0, 1, 0 },   /* peer re-sends: not new         */
        { 0, 0, 1, 1, 1, 0, 1 },   /* peer acks + sends new data     */
        { 1, 1, 0, 0, 1, 1, 0 },   /* empty, new: acked, NESN moves  */
    };
    tiku_radio_ll_ack_t a = { 0u, 0u };
    int i, fails = 0;

    for (i = 0; i < 5; i++) {
        uint8_t r = tiku_radio_ll_ack(&a, seq[i].rx_sn, seq[i].rx_nesn,
                                      seq[i].pay);
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
        SHELL_PRINTF(SH_GREEN "ackfsm: 5/5 incl. retransmission"
                     " and an empty PDU (SN/NESN correct)\n" SH_RST);
    }
}

#if (TIKU_FLPR_ENABLE + 0)
/*---------------------------------------------------------------------------*/
/* FLPR PROBES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief FLPR RADIO RX probe: the coprocessor listens on ch37 for ~4-5 s and
 *        counts address matches and CRC-OK packets.
 *
 * Blocking.  Start a transmitter on the peer board first.
 */
static void rft_flprrx(void)
{
    uint32_t addr_evts = 0u, crcok = 0u, flen = 0u;
    uint8_t  first[16];
    int      rc;

    if (rft_radio_busy()) {
        return;
    }
    /* tiku_flpr_arch_start() gates the probe: its first call after a reset
     * scrubs the shared page and boots the core; a later call restarts a
     * faulted core or resumes a parked one. */
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR failed to start or resume\n");
        return;
    }
    SHELL_PRINTF("FLPR RX probe: listening on ch37 ~4-5 s -- transmit adv on"
                 " the peer (write /sys/radio/beacon TK,20)...\n");
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
        rft_fmt_hex(hx, first, (int)(flen > 16u ? 16u : flen), 0);
        SHELL_PRINTF("  first CRC-ok pkt: %s (S0 LEN S1 ...)\n", hx);
    }
    if (crcok != 0u) {
        SHELL_PRINTF(SH_GREEN "  FLPR RADIO RX WORKS\n" SH_RST);
    } else if (addr_evts != 0u) {
        SHELL_PRINTF("  AA matched but 0 CRC-ok (whitening/format?)\n");
    } else {
        SHELL_PRINTF("  nothing heard (is the peer transmitting on ch37?)\n");
    }
}

/**
 * @brief Build the SCAN_RSP the FLPR advertiser answers a SCAN_REQ with.
 *
 * It carries the NUS service UUID.  A scanner's duplicate filter drops a
 * response that repeats the advert byte for byte, and a host that waits for
 * both then never reports the device.
 *
 * @return Bytes written to @p rsp
 */
static uint8_t rft_flpr_scanrsp(uint8_t *rsp, const uint8_t *addr)
{
    static const uint8_t nus_svc[16] = {
        0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
        0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu };
    uint8_t sd[18];

    sd[0] = 17u;                                 /* length: type + UUID       */
    sd[1] = 0x07u;                               /* complete 128-bit list     */
    memcpy(&sd[2], nus_svc, 16u);
    return tiku_radio_arch_scanrsp_build(rsp, addr, sd, 18u);
}

/**
 * @brief The FLPR advertises TIKU-CONN connectably, captures a CONNECT_IND,
 *        then holds the link on its own for up to 10 s.
 *
 * Connect from any central; the link carries no host, so the central's
 * ATT traffic goes unanswered and it gives up.
 */
static void rft_flpradv(void)
{
    uint8_t addr[6], ad[31], adv[48], rsp[48];
    uint8_t adlen = 0u, advlen, rsplen;
    static const char nm[] = "TIKU-CONN";
    tiku_flpr_conn_info_t info;
    int rc;

    if (rft_radio_busy()) {
        return;
    }
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR failed to start or resume\n");
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
    rsplen = rft_flpr_scanrsp(rsp, addr);
    adv[0] = 0x40u;                              /* ADV_IND (connectable)    */

    SHELL_PRINTF("FLPR advertising 'TIKU-CONN' (connectable) ~8 s -- connect"
                 " from a central...\n");
    tiku_radio_arch_init();
    NRF_RADIO_S->TIFS = 150u;                    /* T_IFS turnaround (hold)  */
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_capture(adv, advlen, rsp, rsplen,
                                     addr, &info);
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
        rft_fmt_hex(aa, aab, 4, 0);
        rft_fmt_hex(ci, cib, 3, 0);
        SHELL_PRINTF(SH_GREEN "  FLPR captured CONNECT_IND: AA=%s CRCInit=%s"
                     " interval=%u hop=%u timeout=%u\n"
                     SH_RST, aa, ci, (unsigned)info.interval,
                     (unsigned)info.hop, (unsigned)info.timeout);
    }
    /* The FLPR holds the link on its own; the M33 only reads the shared
     * event count and link state, once a second for up to 10 s. */
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
    SHELL_PRINTF("  stopped (FLPR serviced %lu events)\n",
                 (unsigned long)tiku_flpr_arch_conn_events());
}

#endif /* TIKU_FLPR_ENABLE */

/*---------------------------------------------------------------------------*/
/* THE COMMAND                                                               */
/*---------------------------------------------------------------------------*/

void tiku_shell_cmd_rftest(uint8_t argc, const char *argv[])
{
    if (argc < 2u) {
        rft_status();
        rft_usage();
        return;
    }

    if (strcmp(argv[1], "cw") == 0) {
        rft_start(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0, 0);
    } else if (strcmp(argv[1], "mod") == 0) {
        rft_start(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0, 1);
    } else if (strcmp(argv[1], "sweep") == 0) {
        rft_sweep(argc > 2u ? argv[2] : (const char *)0,
                  argc > 3u ? argv[3] : (const char *)0,
                  argc > 4u ? argv[4] : (const char *)0);
    } else if (strcmp(argv[1], "off") == 0) {
        if (tiku_radio_arch_carrier_active()) {
            tiku_radio_arch_carrier_stop();
            SHELL_PRINTF("rftest: carrier off, radio released\n");
        } else {
            SHELL_PRINTF("rftest: nothing transmitting\n");
        }
    } else if (strcmp(argv[1], "status") == 0) {
        rft_status();
    } else if (strcmp(argv[1], "phy") == 0) {
        rft_phy_probe();
    } else if (strcmp(argv[1], "phytx") == 0) {
        rft_phytx(argc, argv);
    } else if (strcmp(argv[1], "phyrx") == 0) {
        rft_phyrx(argc, argv);
    } else if (strcmp(argv[1], "ext") == 0) {
        unsigned s = 5u;
        if (argc < 3u) {
            SHELL_PRINTF("usage: rftest ext <name> [secs]\n");
            return;
        }
        if (argc >= 4u) {
            long v = strtol(argv[3], (char **)0, 10);
            if (v > 0 && v <= 120) { s = (unsigned)v; }
        }
        rft_ext(argv[2], s);
    } else if (strcmp(argv[1], "scanreq") == 0) {
        rft_scanreq(argc, argv);
    } else if (strcmp(argv[1], "scan") == 0) {
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
        rft_scan(s, pfx);
    } else if (strcmp(argv[1], "observe") == 0) {
        rft_observe(argc, argv);
    } else if (strcmp(argv[1], "dbg") == 0) {
        rft_dbg();
    } else if (strcmp(argv[1], "csa1") == 0) {
        rft_csa1();
    } else if (strcmp(argv[1], "ackfsm") == 0) {
        rft_ackfsm();
#if (TIKU_FLPR_ENABLE + 0)
    } else if (strcmp(argv[1], "flprrx") == 0) {
        rft_flprrx();
    } else if (strcmp(argv[1], "flpradv") == 0) {
        rft_flpradv();
#endif
    } else {
        rft_usage();
    }
}

#endif /* TIKU_SHELL_CMD_RFTEST */
