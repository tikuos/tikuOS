/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bleadv.c - "bleadv" command: BLE beacon, scan and link tests.
 *
 * Beacon and scan sub-commands use tiku_ble_adv, as BASIC and /sys/radio do;
 * the PHY, link, pairing and FLPR tests drive the Nordic RADIO and FLPR arch
 * layers directly.  Opt-in, and needs a broadcast-capable radio.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_bleadv.h"

#if TIKU_SHELL_CMD_BLEADV

#include <kernel/shell/tiku_shell_io.h>
/* TIKU_CLOCK_ARCH_SECOND, which tiku_clock.h uses but does not define. */
#include <arch/nordic/tiku_timer_arch.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>               /* demo auto-stop timer   */
#include <interfaces/bluetooth/tiku_ble_adv.h>
#include <arch/nordic/tiku_radio_arch.h>            /* RADIO tests, counters  */
#include <arch/nordic/tiku_device_select.h>         /* NRF_* register blocks  */
#include <arch/nordic/tiku_nordic_core.h>           /* wfe (ext pacing)       */
#include <kernel/cpu/tiku_common.h>                 /* unique id -> AdvA      */
#include <interfaces/bluetooth/tiku_ble_smp_pair.h> /* SMP pairing state, LTK */
#include <interfaces/bluetooth/tiku_ble_bond.h>     /* durable LTK bond store */
#if (TIKU_FLPR_ENABLE + 0)
#include <arch/nordic/tiku_flpr_arch.h>             /* FLPR BLE controller    */
#include <interfaces/bluetooth/tiku_ble_serial.h>   /* NUS serial facade      */
#include <interfaces/bluetooth/tiku_ble_host.h>     /* M33 ATT/GATT host      */
#include <interfaces/bluetooth/tiku_ble_smp.h>      /* SMP crypto self-test   */
#include <arch/nordic/tiku_ble_ccm_arch.h>          /* CCM00 hardware CCM     */
#include <interfaces/bluetooth/tiku_ble_enc.h>      /* demo payload + nonce   */
#include <arch/nordic/flpr/tiku_flpr_ipc.h>         /* DLE frame buffer size  */
#endif
#include <kernel/cpu/tiku_watchdog.h>               /* kick in blocking loops */
#include <stdlib.h>
#include <string.h>

/**
 * @brief Parse "AA:BB:CC:DD:EE:FF" (MSB first, as bleadv_fmt_addr prints it)
 *        into packet byte order, out[0] the LSB.
 * @return 1 on success, 0 on a non-hex digit
 */
static int bleadv_parse_addr(const char *s, uint8_t out[6])
{
    int i, hi, lo;
    for (i = 5; i >= 0; i--) {
        if (s[0] == '\0' || s[1] == '\0') {
            return 0;
        }
        hi = (int)*s++;
        lo = (int)*s++;
        hi = (hi >= '0' && hi <= '9') ? hi - '0'
           : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
           : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
        lo = (lo >= '0' && lo <= '9') ? lo - '0'
           : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
           : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out[i] = (uint8_t)((hi << 4) | lo);      /* MSB token -> out[5..0]   */
        if (i && *s == ':') {
            s++;
        }
    }
    return 1;
}

/**
 * @brief Format a 6-byte address held in packet order (LSB first) as
 *        "AA:BB:CC:DD:EE:FF", MSB first; @p out needs 18 bytes.
 */
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

/**
 * @brief Build the SCAN_RSP the FLPR advertisers answer a SCAN_REQ with.
 *
 * It carries the NUS service UUID.  A scanner's duplicate filter drops a
 * response that repeats the advert byte for byte, and a host that waits for
 * both then never reports the device.
 *
 * @return Bytes written to @p rsp
 */
static uint8_t bleadv_flpr_scanrsp(uint8_t *rsp, const uint8_t *addr)
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
 * @brief Format @p n bytes of @p b as hex into @p out (2n + 1 bytes).
 *
 * @p msb_first prints b[n-1]..b[0], the display order of a little-endian
 * field such as the access address; otherwise b[0]..b[n-1].
 */
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

/**
 * @brief 1 if the SPU marks the RADIO secure, so that it answers on the
 *        secure alias; every path that lends the radio to the coprocessor
 *        makes it non-secure, whatever tiku_ble_adv_owner() reports.
 */
static uint8_t radio_is_ours(void)
{
    return (NRF_SPU10_S->PERIPH[10].PERM & (1u << 4)) != 0u;
}

/**
 * @brief Print the FICR words that gate the errata workarounds (see
 *        tiku_crt_early.c), clock-tuning state and RADIO registers, then the
 *        tiku_ble_adv state and the burst, XO, scan and DPPI-window counters.
 */
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
    if (!radio_is_ours()) {
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

        tiku_radio_arch_scan_counts(&isr, &addr, &crcok);
        uint32_t bad = 0u, kind = 0u, named = 0u, kept = 0u;

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

/**
 * @brief Scan 37/38/39 for @p secs and list up to 12 advertisers, only names
 *        starting with @p prefix when it is not NULL.
 */
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
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
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

/**
 * @brief Single-board PHY airtime probe: one burst per PHY on 37/38/39.
 *
 * The polled TX window's iteration count scales with airtime: the same PDU
 * at 2M, S2 and S8 gives about 0.5x, 3x and 8x the 1M count.
 */
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

/** @brief Map "2m", "s8" or "s2" to its PHY; anything else is 1M. */
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

/**
 * @brief Board-to-board PER, transmit side: send N tagged PDUs at a PHY on
 *        ch37 for bleadv phyrx on the peer to count.
 *
 * argv[2] is the PHY (default 1m), argv[3] the count (default 100, at most
 * 5000), argv[4] the payload length (4..36, default 4).
 */
static void bleadv_phytx(uint8_t argc, const char *argv[])
{
    const char *pn = (argc > 2u) ? argv[2] : "1m";
    tiku_radio_arch_phy_t phy = bleadv_phy_of(pn);
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
    if (tiku_ble_adv_active()) {
        SHELL_PRINTF(SH_RED "stop the beacon first (bleadv off)\n" SH_RST);
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

/**
 * @brief Extended advertising for ~@p secs: ADV_EXT_IND plus a hardware-timed
 *        AUX_ADV_IND about every 125 ms.
 *
 * The AdvData (Flags, name and a 47-byte 'TK' payload) exceeds the 31-byte
 * legacy limit.  Blocking; dbg_aux_us reports the aux packet's measured
 * start, which the AuxPtr announces at 600 us.
 */
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

/**
 * @brief Advertise TIKU-CONN for up to @p secs and decode the LLData of the
 *        CONNECT_IND a central sends (bluetoothctl connect).
 *
 * The connection is not accepted, so the central retries and times out.
 * The scan-request and turnaround timing counters are printed after it.
 */
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
        /* LLData layout, little-endian: AA(4) CRCInit(3) WinSize(1)
         * WinOffset(2) Interval(2) Latency(2) Timeout(2) ChM(5)
         * Hop:5|SCA:3(1).  AA and CRCInit print MSB first; ChM prints
         * in on-air byte order. */
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
    SHELL_PRINTF("  timer10 %lu ticks/ms; txen at %lu ticks\n",
                 (unsigned long)tiku_radio_arch_dbg_connadv_ticks_per_ms,
                 (unsigned long)tiku_radio_arch_connadv_txen_ticks);
    SHELL_PRINTF("  rx_tifs=%lu ticks last, %lu..%lu over %lu (a scanner's"
                 " own; the spec says 150 us)\n",
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxtifs,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxtifs_min,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxtifs_max,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxtifs_n);
    SHELL_PRINTF("  adv_tx=%lu scan_req=%lu scan_rsp=%lu tifs=%lu ticks"
                 " rx_other=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_connadv_tx,
                 (unsigned long)tiku_radio_arch_dbg_connadv_scanreq,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rsp,
                 (unsigned long)tiku_radio_arch_dbg_connadv_tifs,
                 (unsigned long)tiku_radio_arch_dbg_connadv_rxother);
}

/**
 * @brief CSA#1 known-answer test (Core 4.5.8.2): eight hops on each of three
 *        channel maps, the last two exercising the remap path.
 */
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
            last = un;                  /* advance by unmapped, not ch  */
        }
    }
    if (fails == 0) {
        SHELL_PRINTF(SH_GREEN "csa1: 24/24 hops match the independent"
                     " reference\n" SH_RST);
    }
}

/**
 * @brief Central role: connect to the first advertiser named TIKU* (bleadv
 *        conn or flprnus on the peer) and drive the link for up to @p secs.
 *
 * Reports connection events, responses heard, the peer's T_IFS, LL control
 * traffic and the NUS/GATT client results.
 *
 * @param updates  Non-zero sends a channel-map and a connection update
 */
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
    tiku_radio_arch_central_updates(updates);     /* arm LL updates          */
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
    SHELL_PRINTF("  LL ctrl: tx=%lu rx=%lu peer_version=%u\n",
                 (unsigned long)st.ctrl_tx, (unsigned long)st.ctrl_rx,
                 (unsigned)st.peer_vers);
    {
        char rb[3];
        bleadv_fmt_hex(rb, &st.att_readback, 1, 0);
        if (st.att_step >= 8u && st.att_ok) {
            SHELL_PRINTF(SH_GREEN "  NUS: MTU/discover/CCCD/write->notify"
                         " loopback OK, echo[0]=0x%s\n" SH_RST, rb);
        } else {
            SHELL_PRINTF("  NUS: incomplete (step=%u/8 echo[0]=0x%s)\n",
                         (unsigned)st.att_step, rb);
        }
        SHELL_PRINTF("  GATT discovery: %s (handles RX/TX/CCCD matched)"
                     "\n", st.att_disc ? "OK" : "not matched");
    }
    if (st.att_lread || st.att_lwrite) {
        SHELL_PRINTF("  GATT long ops: read-blob=%s prep/exec-write=%s"
                     " (GATT database)\n",
                     st.att_lread ? "OK" : "FAIL",
                     st.att_lwrite ? "OK" : "FAIL");
    }
    SHELL_PRINTF("  ended: %s\n",
                 st.reason == 0u ? "duration cap" :
                 st.reason == 1u ? "supervision (peripheral silent)" :
                 st.reason == 3u ? "peer terminated" :
                                   "never connected");
}

/**
 * @brief Central as SMP initiator: connect to a TIKU* advertiser (bleadv
 *        flprpair on the peer) and pair with LE Secure Connections, Numeric
 *        Comparison when @p numcmp, else Just Works.
 *
 * Prints the LTK, which the peripheral prints too for comparison, then the
 * session key once LL encryption has started.
 */
static void bleadv_censmp(unsigned secs, uint8_t numcmp)
{
    uint8_t addr[6];
    tiku_radio_ll_conn_stats_t st;
    char addrstr[18];

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;                             /* static random address    */
    bleadv_fmt_addr(addrstr, addr);
    tiku_ble_smp_pair_reset();                    /* clear prior LTK/state    */
    tiku_ble_smp_pair_set_method(numcmp);         /* Just Works / Num Comp    */
    tiku_radio_arch_central_smp(1u);              /* arm the initiator        */
    SHELL_PRINTF("CENTRAL %s: scanning for TIKU-PAIR, LE-SC pairing up to"
                 " %u s...\n", addrstr, secs);
    (void)tiku_radio_arch_central(addr, secs, &st);
    tiku_radio_arch_central_smp(0u);              /* disarm for the next run  */

    SHELL_PRINTF("  events=%lu rx_ok=%lu (%lu%% responded)\n",
                 (unsigned long)st.events, (unsigned long)st.rx_ok,
                 st.events ? (unsigned long)(st.rx_ok * 100u / st.events)
                           : 0ul);
    {   /* Numeric Comparison: the six digits the peer prints too. */
        uint32_t cmp;
        if (numcmp && tiku_ble_smp_pair_compare_value(&cmp) == 0) {
            SHELL_PRINTF("  COMPARE: %06lu\n", (unsigned long)cmp);
        }
    }
    if (tiku_ble_smp_pair_state() == TIKU_BLE_SMP_STATE_DONE) {
        uint8_t ltk[16], sk[16];
        char hx[40];
        (void)tiku_ble_smp_pair_ltk(ltk);
        bleadv_fmt_hex(hx, ltk, 16, 0);
        SHELL_PRINTF(SH_GREEN "  SMP OK: LTK=%s\n" SH_RST, hx);
        if (tiku_radio_arch_central_enc(sk)) {   /* LL_ENC -> session key     */
            bleadv_fmt_hex(hx, sk, 16, 0);
            SHELL_PRINTF(SH_GREEN "  ENC OK: SK=%s\n" SH_RST, hx);
            SHELL_PRINTF("  ENC-DATA: sent 1 CCM-encrypted payload"
                         " (peer decrypts)\n");
        } else {
            SHELL_PRINTF("  ENC: LL encryption did not complete\n");
        }
    } else {
        SHELL_PRINTF(SH_RED "  SMP pairing did not complete (state=%d)\n"
                     SH_RST, (int)tiku_ble_smp_pair_state());
    }
}

/**
 * @brief Central with bonding: the first connection to a peer pairs (LE-SC)
 *        and stores its LTK; a reconnect skips pairing and reuses it.
 *
 * Bonds live in a durable persist cell and survive a reboot.  A @p target
 * that parses as AA:BB:CC:DD:EE:FF selects the peer by address instead of
 * by the TIKU* name.
 */
static void bleadv_cenbond(unsigned secs, const char *target)
{
    uint8_t addr[6], sk[16], taddr[6];
    tiku_radio_ll_conn_stats_t st;
    char addrstr[18];
    int bonded, by_addr = 0;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    bleadv_fmt_addr(addrstr, addr);
    tiku_ble_smp_pair_reset();
    tiku_radio_arch_central_bond(1u);             /* arm pair+remember/reuse  */
    /* Optional scan by address: connect to a specific AdvA instead of the
     * "TIKU" name. */
    if (target != (const char *)0 && bleadv_parse_addr(target, taddr)) {
        tiku_radio_arch_central_target(taddr);
        by_addr = 1;
    } else {
        tiku_radio_arch_central_target((const uint8_t *)0);
    }
    SHELL_PRINTF("CENTRAL %s: %s, bonding (bonds=%u) up to %u s...\n", addrstr,
                 by_addr ? "connecting by ADDRESS" : "scanning for TIKU-PAIR",
                 (unsigned)tiku_ble_bond_count(), secs);
    (void)tiku_radio_arch_central(addr, secs, &st);
    bonded = tiku_radio_arch_central_bonded();
    tiku_radio_arch_central_bond(0u);
    tiku_radio_arch_central_target((const uint8_t *)0);   /* clear filter    */

    SHELL_PRINTF("  events=%lu rx_ok=%lu (%lu%% responded) AA=%08lx\n",
                 (unsigned long)st.events, (unsigned long)st.rx_ok,
                 st.events ? (unsigned long)(st.rx_ok * 100u / st.events)
                           : 0ul,
                 (unsigned long)tiku_radio_arch_dbg_cen_aa);
    if (tiku_radio_arch_central_enc(sk)) {
        char hx[40];
        bleadv_fmt_hex(hx, sk, 16, 0);
        if (bonded) {
            SHELL_PRINTF(SH_GREEN "  BOND OK: reused stored LTK (skipped "
                         "pairing), SK=%s\n" SH_RST, hx);
        } else {
            SHELL_PRINTF(SH_GREEN "  PAIRED+STORED: fresh LTK bonded, SK=%s\n"
                         SH_RST, hx);
        }
        SHELL_PRINTF("  bonds=%u\n", (unsigned)tiku_ble_bond_count());
    } else {
        SHELL_PRINTF(SH_RED "  encryption did not complete (bonded=%d)\n" SH_RST,
                     bonded);
    }
}

/**
 * @brief Central with a PHY update: connect to a TIKU* advertiser, run the NUS
 *        loopback, then switch PHY (LL_PHY_REQ/RSP, UPDATE_IND at an Instant).
 *
 * Success is the link still serviced more than 20 events after the switch.
 *
 * @param target  1 = 2M, 2 = Coded S8
 */
static void bleadv_cenphy(unsigned secs, uint8_t target)
{
    uint8_t addr[6];
    tiku_radio_ll_conn_stats_t st;
    char addrstr[18];
    const char *pn = (target == 2u) ? "CODED-S8" : "2M";
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
    bleadv_fmt_addr(addrstr, addr);
    tiku_radio_arch_central_phy(target);          /* arm the PHY update       */
    SHELL_PRINTF("CENTRAL %s: scanning for TIKU-CONN, will update PHY -> %s"
                 " up to %u s...\n", addrstr, pn, secs);
    rc = tiku_radio_arch_central(addr, secs, &st);
    tiku_radio_arch_central_phy(0u);
    if (rc != 0) {
        SHELL_PRINTF("no TIKU-CONN peripheral found in %u s\n", secs);
        return;
    }
    SHELL_PRINTF("  events=%lu rx_ok=%lu (%lu%% responded)\n",
                 (unsigned long)st.events, (unsigned long)st.rx_ok,
                 st.events ? (unsigned long)(st.rx_ok * 100u / st.events)
                           : 0ul);
    {
        uint16_t survived = 0u;
        if (tiku_radio_arch_central_phy_result(&survived) && survived > 20u) {
            SHELL_PRINTF(SH_GREEN "  PHY OK: switched to %s, survived %u events"
                         " on %s\n" SH_RST, pn, (unsigned)survived, pn);
        } else {
            uint32_t dbg = tiku_radio_arch_dbg_phy;
            SHELL_PRINTF(SH_RED "  PHY: did not survive (survived=%u; stage=%lu"
                         " att=%lu rsp=%lu mode=%lu)\n" SH_RST,
                         (unsigned)survived, (unsigned long)(dbg & 0xFFu),
                         (unsigned long)((dbg >> 8) & 0xFFu),
                         (unsigned long)((dbg >> 16) & 0xFFu),
                         (unsigned long)((dbg >> 24) & 0xFFu));
        }
    }
}

/**
 * @brief Peripheral role: advertise TIKU-CONN, accept a central (bleadv
 *        central on a peer, or a phone) and hold the link for up to @p secs.
 *
 * Blocks the shell while connected, then prints the link statistics.
 */
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
    SHELL_PRINTF("  LL ctrl: tx=%lu rx=%lu peer_version=%u\n",
                 (unsigned long)st.ctrl_tx, (unsigned long)st.ctrl_rx,
                 (unsigned)st.peer_vers);
    if (st.att_readback != 0u) {
        char wv[3];
        bleadv_fmt_hex(wv, &st.att_readback, 1, 0);
        SHELL_PRINTF("  NUS server: last RX write[0]=0x%s at handle 0x0012"
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
 * timer, which runs while the shell idles at the prompt.  The bursts come
 * from tiku_ble_adv's own timer, or from the FLPR when it runs the beacon. */
static struct tiku_timer bleadv_stop_timer;

/** @brief Demo auto-stop: print the burst count and stop the beacon. */
static void bleadv_autostop_cb(void *ptr)
{
    (void)ptr;
    SHELL_PRINTF("\nbleadv: beacon done (%lu bursts total)\n",
                 (unsigned long)tiku_ble_adv_bursts());
    tiku_ble_adv_stop();
}

#if (TIKU_FLPR_ENABLE + 0)
/**
 * @brief FLPR RADIO RX probe: the coprocessor listens on ch37 for ~4-5 s and
 *        counts address matches and CRC-OK packets.
 *
 * Blocking.  Start a transmitter on the peer board first, e.g. bleadv on TK.
 */
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
    /* tiku_flpr_arch_start() gates the probe: its first call after a reset
     * scrubs the shared page and boots the core; a later call restarts a
     * faulted core or resumes a parked one. */
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR failed to start or resume\n");
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
        SHELL_PRINTF(SH_GREEN "  FLPR RADIO RX WORKS\n"
                     SH_RST);
    } else if (addr_evts != 0u) {
        SHELL_PRINTF("  AA matched but 0 CRC-ok (whitening/format?)\n");
    } else {
        SHELL_PRINTF("  nothing heard (is the peer transmitting on ch37?)\n");
    }
}

/**
 * @brief The FLPR advertises TIKU-CONN connectably, captures a CONNECT_IND,
 *        then holds the link on its own for up to 10 s.
 *
 * Connect from a central, e.g. bleadv central on the peer.
 */
static void bleadv_flpradv(void)
{
    uint8_t addr[6], ad[31], adv[48], rsp[48];
    uint8_t adlen = 0u, advlen, rsplen;
    static const char nm[] = "TIKU-CONN";
    tiku_flpr_conn_info_t info;
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
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
    rsplen = bleadv_flpr_scanrsp(rsp, addr);
    adv[0] = 0x40u;                              /* ADV_IND (connectable)    */

    SHELL_PRINTF("FLPR advertising 'TIKU-CONN' (connectable) ~8 s -- connect"
                 " from a central (bleadv central on the peer)...\n");
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
        bleadv_fmt_hex(aa, aab, 4, 0);
        bleadv_fmt_hex(ci, cib, 3, 0);
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

/**
 * @brief CCM00 hardware AES-CCM self-test against the software CCM: encrypt
 *        match, decrypt round-trip with MIC check, tampered MIC rejected.
 */
static void bleadv_ccmhw(void)
{
    int r = tiku_ble_ccm_arch_selftest();
    SHELL_PRINTF("CCM00 hardware AES-CCM self-test:\n");
    SHELL_PRINTF("  encrypt == software CCM:   %s\n",
                 (r & 1) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  decrypt + MIC verified:    %s\n",
                 (r & 2) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  tampered MIC rejected:     %s\n",
                 (r & 4) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    if (r == 7) {
        SHELL_PRINTF(SH_GREEN "  CCM00 OK: typed job lists + reversed "
                     "KEY/NONCE -- inline LL crypt buildable\n" SH_RST);
    }
}

/**
 * @brief SMP LESC crypto self-test: AES-CMAC (RFC 4493), f4/f5/f6 and g2
 *        known answers, and a P-256 ECDH round-trip.
 */
static void bleadv_smp(void)
{
    int r = tiku_ble_smp_selftest();
    SHELL_PRINTF("SMP LESC crypto self-test:\n");
    SHELL_PRINTF("  AES-CMAC (RFC 4493 KAT):   %s\n",
                 (r & 1) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  f4/f5/f6 (Core-spec KAT):  %s\n",
                 (r & 4) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  P-256 ECDH (round-trip):   %s\n",
                 (r & 2) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    SHELL_PRINTF("  g2 numeric-compare (KAT):  %s\n",
                 (r & 8) ? SH_GREEN "PASS" SH_RST : SH_RED "FAIL" SH_RST);
    if (r == 15) {
        SHELL_PRINTF(SH_GREEN "  crypto foundation OK (CMAC + f4/f5/f6 + g2 +"
                     " ECDH)\n" SH_RST);
    }
}

/**
 * @brief Send the host's pending TX PDU to the FLPR as fragments tagged with
 *        their LLID (2 start, 1 continuation), waiting while the mailbox is
 *        full; repeats while the SMP engine has another PDU queued.
 */
static void bleadv_flpr_drain_tx(void)
{
    uint8_t  frag[32], llid;
    uint16_t fl;
    do {
        while ((fl = tiku_ble_host_next_tx(frag, sizeof(frag), &llid)) > 0u) {
            while (tiku_flpr_arch_conn_send(frag, fl, llid) == -2 &&
                   tiku_flpr_arch_conn_active()) {
                tiku_watchdog_kick();
            }
            if (!tiku_flpr_arch_conn_active()) {
                return;
            }
        }
        /* TX drained: stage the next SMP PDU (if the pairing engine has one
         * queued, e.g. the responder's Public Key then Confirm) and resend. */
    } while (tiku_ble_host_smp_pump() != 0);
}

/**
 * @brief The FLPR advertises TIKU-CONN and carries NUS data for up to 15 s:
 *        bytes a central writes come back as notifications from the M33 host.
 *
 * Then reports the anchored-RX duty, the LL updates followed, and the DLE
 * and PHY-update results.
 *
 * @param req_cpu  Non-zero asks the central for a longer interval (L2CAP
 *                 Connection Parameter Update Request) once it subscribes
 */
static void bleadv_flprnus(uint8_t req_cpu)
{
    uint8_t addr[6], ad[31], adv[48], rsp[48];
    uint8_t cpu_sent = 0u;
    uint8_t adlen = 0u, advlen, rsplen;
    static const char nm[] = "TIKU-CONN";
    tiku_flpr_conn_info_t info;
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
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
    rsplen = bleadv_flpr_scanrsp(rsp, addr);
    adv[0] = 0x40u;

    SHELL_PRINTF("FLPR NUS pipe: advertising 'TIKU-CONN' -- connect a NUS"
                 " client (bleadv central on the peer)...\n");
    tiku_radio_arch_init();
    NRF_RADIO_S->TIFS = 150u;
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_capture(adv, advlen, rsp, rsplen,
                                     addr, &info);
    if (rc != 0) {
        uint32_t atx = 0u, sq = 0u, sr = 0u, ot = 0u;
        tiku_radio_arch_constlat_hold(0);
        tiku_flpr_arch_adv_counts(&atx, &sq, &sr, &ot);
        SHELL_PRINTF("no NUS client connected (rc=%d)\n", rc);
        SHELL_PRINTF("  adv: tx=%lu scan_req=%lu scan_rsp=%lu other=%lu"
                     " rsp_at=%lu\n",
                     (unsigned long)atx, (unsigned long)sq,
                     (unsigned long)sr, (unsigned long)ot,
                     (unsigned long)tiku_flpr_arch_adv_tifs());
        return;
    }
    {
        uint32_t atx = 0u, sq = 0u, sr = 0u, ot = 0u;
        tiku_flpr_arch_adv_counts(&atx, &sq, &sr, &ot);
        SHELL_PRINTF("  adv: tx=%lu scan_req=%lu scan_rsp=%lu other=%lu\n",
                     (unsigned long)atx, (unsigned long)sq,
                     (unsigned long)sr, (unsigned long)ot);
    }
    SHELL_PRINTF("  connected; M33 NUS host live (ATT on M33), echoing"
                 " RX->TX ~15 s...\n");
    {
        uint8_t frame[TIKU_FLPR_DLE_MAX_OCTETS];  /* holds a DLE-sized LL PDU */
        uint8_t nus[TIKU_BLE_HOST_MTU];
        char hx[50];
        uint32_t total = 0u;
        tiku_clock_time_t start = tiku_clock_time();
        tiku_ble_host_reset();                    /* M33 ATT/GATT host        */
        while ((tiku_clock_time_t)(tiku_clock_time() - start) <
               (tiku_clock_time_t)(TIKU_CLOCK_SECOND * 15u)) {
            /* Pump: L2CAP fragment in -> recombine + ATT/GATT on the M33 ->
             * response fragmented out.  A (possibly multi-fragment) NUS RX
             * write surfaces bytes echoed back as a notification. */
            uint8_t llid_in;
            int n;
            uint32_t dm = tiku_flpr_arch_dle_max(); /* 0 until DLE is agreed */
            if (dm > 27u) {
                tiku_ble_host_set_frag_max((uint8_t)dm);
            }
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
            /* Once subscribed, ask the central (L2CAP signalling) for a
             * longer interval.  A central that accepts issues an LL
             * connection update, which the FLPR follows (cu below rises). */
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
            tiku_flpr_conn_timing_t ct;
            tiku_flpr_arch_conn_timing(&ct);
            SHELL_PRINTF("  timing: interval=%u winoffset=%u winsize=%u "
                         "first-catch=%lu misses=%lu late-opens=%lu "
                         "late-replies=%lu widening=%luus\n",
                         (unsigned)ct.interval, (unsigned)ct.winoffset,
                         (unsigned)ct.winsize, (unsigned long)ct.first,
                         (unsigned long)ct.misses,
                         (unsigned long)ct.late_opens,
                         (unsigned long)ct.late_tx,
                         (unsigned long)ct.widen_us);
        }
        {
            uint32_t cm = 0u, cu = 0u;
            uint32_t evt = tiku_flpr_arch_conn_events();
            (void)tiku_flpr_arch_conn_updates(&cm, &cu);
            /* The FLPR advances events only while it keeps catching the
             * central, so a mis-applied update shows as a count that stops
             * near its Instant.  cm/cu count the updates it followed. */
            SHELL_PRINTF("  events=%lu  LL-updates: channel-map=%lu"
                         " connection=%lu\n",
                         (unsigned long)evt, (unsigned long)cm,
                         (unsigned long)cu);
            if (req_cpu) {
                if (cu != 0u) {
                    SHELL_PRINTF(SH_GREEN "  Connection update OK: peripheral-requested"
                                 " reparametrise -- central obliged, FLPR"
                                 " followed the LL update\n" SH_RST);
                } else {
                    SHELL_PRINTF("  Conn Param Update: not followed (central"
                                 " declined, or no LL update seen)\n");
                }
            } else if (cm != 0u && cu != 0u) {
                SHELL_PRINTF(SH_GREEN "  Link update OK: link survived a"
                             " mid-connection channel-map + interval change"
                             "\n" SH_RST);
            } else {
                SHELL_PRINTF("  LL updates: incomplete (central sent none, or"
                             " the peer lacks update support)\n");
            }
        }
        tiku_flpr_arch_conn_stop();
        tiku_radio_arch_constlat_hold(0);
        if (total != 0u) {
            SHELL_PRINTF(SH_GREEN "  NUS pipe OK: %lu bytes RX'd + echoed"
                         " through the FLPR\n" SH_RST,
                         (unsigned long)total);
        } else {
            SHELL_PRINTF("  no NUS bytes received (client didn't write?)\n");
        }
        {   /* DLE: negotiated, and an L2CAP PDU over 31 bytes arrived whole
             * in one LL PDU, more than a 27-byte legacy LL payload holds. */
            uint32_t dm = tiku_flpr_arch_dle_max();
            uint16_t single = tiku_ble_host_max_single_frag();
            if (dm > 27u && single > 31u) {
                SHELL_PRINTF(SH_GREEN "  DLE OK: max=%lu octets, a %u-byte "
                             "L2CAP PDU rode ONE LL PDU\n" SH_RST,
                             (unsigned long)dm, (unsigned)single);
            } else {
                SHELL_PRINTF("  DLE: max=%lu single-frag=%u (not exercised)\n",
                             (unsigned long)dm, (unsigned)single);
            }
        }
        {   /* PHY update applied by the FLPR at its Instant: the events
             * serviced since the switch show whether the link held. */
            uint32_t phy_at = 0u;
            uint32_t phy = tiku_flpr_arch_conn_phy(&phy_at);
            uint32_t ev  = tiku_flpr_arch_conn_events();
            uint32_t pm = 0u, pa = 0u, pc = 0u;
            const char *pn = (phy == 2u) ? "CODED-S8" : "2M";
            tiku_flpr_arch_conn_phy_diag(&pm, &pa, &pc);
            if (phy != 0u && ev > phy_at + 20u) {
                SHELL_PRINTF(SH_GREEN "  PHY OK: switched to %s, survived %lu"
                             " events on %s\n" SH_RST, pn,
                             (unsigned long)(ev - phy_at), pn);
            } else if (phy != 0u) {
                SHELL_PRINTF("  PHY: switched to %s, survived %lu events\n",
                             pn, (unsigned long)(ev - phy_at));
            }
            if (phy != 0u) {                /* PHY diagnostics        */
                SHELL_PRINTF("  PHY diag: mode=%lu addr=%lu crcok=%lu\n",
                             (unsigned long)pm, (unsigned long)pa,
                             (unsigned long)pc);
            }
        }
    }
    /* Park the FLPR's hold loop, reclaim the secure RADIO alias and release
     * constlat.  Otherwise the FLPR keeps servicing the link and owns the
     * non-secure RADIO, the next run's re-init writes to the secure alias are
     * blocked, and link config such as a 2M MODE leaks into the next
     * advertising session. */
    tiku_flpr_arch_conn_stop();
    tiku_radio_arch_constlat_hold(0);
}

/**
 * @brief SMP responder: the FLPR advertises TIKU-PAIR and holds the link while
 *        the M33 host pairs over CID 0x0006 with LE Secure Connections.
 *
 * Prints the LTK (the central prints its own, for comparison), the session
 * key, and whether one CCM-encrypted payload from the central decrypted.
 *
 * @param bond_mode  Non-zero stores the LTK against the central's address;
 *                   a known central then skips pairing and reuses it
 * @param numcmp     Non-zero selects Numeric Comparison, else Just Works
 */
static void bleadv_flprpair(uint8_t bond_mode, uint8_t numcmp)
{
    uint8_t addr[6], ad[31], adv[48], rsp[48];
    uint8_t adlen = 0u, advlen, rsplen;
    static const char nm[] = "TIKU-PAIR";
    tiku_flpr_conn_info_t info;
    int rc;

    if (tiku_ble_adv_owner() != TIKU_BLE_ADV_OWNER_IDLE) {
        SHELL_PRINTF(SH_RED "radio busy (%s)\n" SH_RST,
                     tiku_ble_adv_owner_str());
        return;
    }
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        SHELL_PRINTF("FLPR failed to start or resume\n");
        return;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;                             /* static random address    */
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;
    ad[adlen++] = (uint8_t)(1u + sizeof(nm) - 1u);
    ad[adlen++] = 0x09u;
    memcpy(&ad[adlen], nm, sizeof(nm) - 1u);
    adlen = (uint8_t)(adlen + sizeof(nm) - 1u);
    advlen = tiku_radio_arch_adv_build(adv, addr, ad, adlen);
    rsplen = bleadv_flpr_scanrsp(rsp, addr);
    adv[0] = 0x40u;                               /* ADV_IND, random TxAdd    */

    {   /* Print the AdvA so a peer can connect to it by address. */
        char astr[18];
        bleadv_fmt_addr(astr, addr);
        SHELL_PRINTF("FLPR SMP pair: advertising 'TIKU-PAIR' as %s -- run "
                     "'bleadv censmp' on the peer...\n", astr);
    }
    tiku_radio_arch_init();
    NRF_RADIO_S->TIFS = 150u;
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_capture(adv, advlen, rsp, rsplen,
                                     addr, &info);
    if (rc != 0) {
        tiku_radio_arch_constlat_hold(0);
        SHELL_PRINTF("no central connected (rc=%d)\n", rc);
        return;
    }
    {
        uint8_t frame[TIKU_FLPR_DLE_MAX_OCTETS];  /* holds a DLE-sized LL PDU */
        uint8_t inita[6], adva[6], types;
        uint8_t ltk[16], sk[16], iv[8];
        int paired = 0, enc_done = 0, dec_ok = 0, bonded = 0, bond_saved = 0;
        tiku_clock_time_t start = tiku_clock_time();

        tiku_ble_host_reset();
        types = tiku_flpr_arch_conn_addrs(inita, adva);   /* A=InitA, B=AdvA  */
        /* Bonding: a known central skips pairing -- load its stored LTK and
         * go straight to the LL-encryption wait (no SMP responder armed). */
        if (bond_mode &&
            tiku_ble_bond_find(inita, (uint8_t)(types & 1u), ltk)) {
            bonded = 1; paired = 1;
            SHELL_PRINTF("  connected; BONDED central %02x%02x%02x%02x%02x%02x"
                         " -- skipping pairing\n", inita[5], inita[4], inita[3],
                         inita[2], inita[1], inita[0]);
        } else {
            tiku_ble_smp_pair_set_method(numcmp);  /* Just Works / Num Comp */
            tiku_ble_host_smp_start(inita, (uint8_t)(types & 1u),
                                    adva, (uint8_t)((types >> 1) & 1u));
            SHELL_PRINTF("  connected; responder armed (central %02x%02x%02x%02x"
                         "%02x%02x) -- pairing...\n", inita[5], inita[4],
                         inita[3], inita[2], inita[1], inita[0]);
        }

        while ((tiku_clock_time_t)(tiku_clock_time() - start) <
               (tiku_clock_time_t)(TIKU_CLOCK_SECOND * 15u)) {
            uint8_t llid_in;
            int n;
            uint32_t dm = tiku_flpr_arch_dle_max(); /* 0 until DLE is agreed */
            if (dm > 27u) {
                tiku_ble_host_set_frag_max((uint8_t)dm);
            }
            bleadv_flpr_drain_tx();                /* flush any staged reply  */
            n = tiku_flpr_arch_conn_recv(frame, sizeof(frame), &llid_in);
            if (n > 0) {
                tiku_ble_host_rx(frame, (uint16_t)n, llid_in);
                bleadv_flpr_drain_tx();            /* send SMP response(s)    */
            }
            /* On DONE, success is marked and the link is still served: the
             * final DHKey Check must still go over the air, and a central
             * that lost it re-requests (dup Ea -> engine re-emits Eb).  The
             * loop ends when the central drops the link or after 15 s. */
            if (tiku_ble_host_smp_state() == 2 && !paired) {
                uint32_t cmp;
                paired = 1;
                if (numcmp &&
                    tiku_ble_smp_pair_compare_value(&cmp) == 0) {
                    SHELL_PRINTF("  COMPARE: %06lu\n", (unsigned long)cmp);
                }
                (void)tiku_ble_host_smp_ltk(ltk);
            }
            /* Fresh pairing done: store the LTK once, durably, so the next
             * reconnect from this central skips SMP (bonding). */
            if (bond_mode && paired && !bonded && !bond_saved) {
                (void)tiku_ble_bond_store(inita, (uint8_t)(types & 1u), ltk);
                bond_saved = 1;
            }
            /* Once paired, derive the session key when the central starts
             * LL encryption (the FLPR forwards SKDm/IVm here). */
            if (paired && !enc_done && tiku_flpr_arch_enc_service(ltk)) {
                enc_done = 1;
                tiku_flpr_arch_enc_sk(sk);
                tiku_flpr_arch_enc_iv(iv);
            }
            /* The central's CCM-encrypted payload arrives as a NUS write.
             * Decrypt with SK, verify the MIC, compare with the known text. */
            if (enc_done && !dec_ok) {
                uint8_t cbuf[TIKU_BLE_HOST_MTU];
                uint16_t m = tiku_ble_host_nus_recv(cbuf, sizeof(cbuf));
                if (m == (uint16_t)(TIKU_BLE_ENC_DEMO_PT_LEN + 4u)) {
                    static const uint8_t want[TIKU_BLE_ENC_DEMO_PT_LEN] =
                        TIKU_BLE_ENC_DEMO_PT;
                    uint8_t nonce[13], aad = TIKU_BLE_ENC_DEMO_AAD;
                    uint8_t pt[TIKU_BLE_ENC_DEMO_PT_LEN];
                    tiku_ble_enc_nonce(nonce, 0u, 1u, iv);  /* central->local */
                    /* Decrypt and verify the MIC on CCM00, the
                     * RADIO-companion hardware engine. */
                    if (tiku_ble_ccm_arch_crypt(1, sk, nonce, aad, cbuf,
                            TIKU_BLE_ENC_DEMO_PT_LEN, pt) == 0 &&
                        memcmp(pt, want, TIKU_BLE_ENC_DEMO_PT_LEN) == 0) {
                        dec_ok = 1;
                    }
                }
            }
            tiku_watchdog_kick();
            if (!tiku_flpr_arch_conn_active()) {
                break;
            }
        }
        tiku_flpr_arch_conn_stop();
        tiku_radio_arch_constlat_hold(0);
        if (bonded || (paired && tiku_ble_host_smp_state() == 2)) {
            char hx[40];
            bleadv_fmt_hex(hx, ltk, 16, 0);
            if (bonded) {
                SHELL_PRINTF(SH_GREEN "  BOND OK: reused stored LTK=%s "
                             "(skipped pairing)\n" SH_RST, hx);
            } else {
                SHELL_PRINTF(SH_GREEN "  SMP OK: LTK=%s\n" SH_RST, hx);
            }
            if (enc_done) {
                bleadv_fmt_hex(hx, sk, 16, 0);
                SHELL_PRINTF(SH_GREEN "  ENC OK: SK=%s\n" SH_RST, hx);
                if (dec_ok) {
                    SHELL_PRINTF(SH_GREEN "  ENC-DATA OK: decrypted+MIC-verified"
                                 " 'TIKU-LL-CCM-DEMO'\n" SH_RST);
                } else {
                    SHELL_PRINTF("  ENC-DATA: no valid ciphertext received\n");
                }
            } else {
                SHELL_PRINTF("  ENC: no LL_ENC_REQ from central\n");
            }
            if (bond_mode) {
                SHELL_PRINTF("  bonds=%u\n", (unsigned)tiku_ble_bond_count());
            }
        } else {
            SHELL_PRINTF(SH_RED "  SMP pairing did not complete (state=%d)\n"
                         SH_RST, tiku_ble_host_smp_state());
        }
    }
}

/**
 * @brief NUS echo service through the tiku_ble_serial facade for ~@p secs.
 *
 * The facade re-advertises after a central drops, so "link up (#N)" with
 * N >= 2 shows a reconnect.  The report also reads the FLPR's connection
 * state and advertising counters directly.
 */
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
    {
        uint32_t atx = 0u, sq = 0u, sr = 0u, ot = 0u;
        tiku_flpr_arch_adv_counts(&atx, &sq, &sr, &ot);
        SHELL_PRINTF("  adv: tx=%lu scan_req=%lu scan_rsp=%lu other=%lu"
                     " rsp_at=%lu state=%lu\n",
                     (unsigned long)atx, (unsigned long)sq,
                     (unsigned long)sr, (unsigned long)ot,
                     (unsigned long)tiku_flpr_arch_adv_tifs(),
                     (unsigned long)tiku_flpr_arch_conn_state());
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
    if (strcmp(argv[1], "smp") == 0) {            /* SMP crypto self-test     */
        bleadv_smp();
        return;
    }
    if (strcmp(argv[1], "ccmhw") == 0) {          /* CCM00 hardware CCM KAT   */
        bleadv_ccmhw();
        return;
    }
    if (strcmp(argv[1], "flprnus") == 0) {
        if (argc >= 3) {                          /* ticks to reply's TXEN    */
            long c = strtol(argv[2], (char **)0, 10);
            if (c >= 20 && c <= 2000) {
                tiku_flpr_arch_adv_txen_ticks = (uint32_t)c;
            }
        }
        bleadv_flprnus(0u);
        return;
    }
    if (strcmp(argv[1], "flprcpu") == 0) {        /* + conn param update req  */
        bleadv_flprnus(1u);
        return;
    }
    if (strcmp(argv[1], "flprpair") == 0) {       /* SMP responder            */
        uint8_t nc = (argc >= 3 && strcmp(argv[2], "numcmp") == 0) ? 1u : 0u;
        bleadv_flprpair(0u, nc);
        return;
    }
    if (strcmp(argv[1], "flprbond") == 0) {       /* bonding: remember/reuse  */
        if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
            tiku_ble_bond_clear();
            SHELL_PRINTF("bonds cleared\n");
            return;
        }
        bleadv_flprpair(1u, 0u);
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
        /* Scripted peer sequence with a retransmission at step 2, and the
         * expected (new, acked, sn, nesn) after each RX. */
        static const struct {
            uint8_t rx_sn, rx_nesn, newd, ackd, sn, nesn;
        } seq[4] = {
            { 0, 0, 1, 0, 0, 1 },   /* first packet: new, no ack yet     */
            { 1, 1, 1, 1, 1, 0 },   /* peer acks + sends new data        */
            { 1, 1, 0, 0, 1, 0 },   /* peer re-sends: not new, not acked */
            { 0, 0, 1, 1, 0, 1 },   /* peer acks + sends new data again  */
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
    if (strcmp(argv[1], "cenupd") == 0) {         /* central + LL updates     */
        unsigned s = 30u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        bleadv_central(s, 1u);
        return;
    }
    if (strcmp(argv[1], "censmp") == 0) {         /* SMP initiator            */
        unsigned s = 30u;
        uint8_t nc = 0u, a;
        for (a = 2u; a < argc; a++) {
            if (strcmp(argv[a], "numcmp") == 0) {
                nc = 1u;
            } else {
                long v = strtol(argv[a], (char **)0, 10);
                if (v > 0 && v <= 600) { s = (unsigned)v; }
            }
        }
        bleadv_censmp(s, nc);
        return;
    }
    if (strcmp(argv[1], "cenbond") == 0) {        /* bonding: pair/reuse LTK  */
        unsigned s = 30u;
        const char *target = (const char *)0;
        if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
            tiku_ble_bond_clear();
            SHELL_PRINTF("bonds cleared\n");
            return;
        }
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        if (argc >= 4) {                           /* scan-by-address arg     */
            target = argv[3];
        }
        bleadv_cenbond(s, target);
        return;
    }
    if (strcmp(argv[1], "cenphy") == 0) {   /* PHY update (2M / coded)      */
        unsigned s = 30u;
        uint8_t target = 1u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 600) { s = (unsigned)v; }
        }
        if (argc >= 4 && strcmp(argv[3], "coded") == 0) {
            target = 2u;
        }
        bleadv_cenphy(s, target);
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
    if (strcmp(argv[1], "scanreq") == 0) {        /* active scanner timing    */
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
                tiku_radio_arch_connadv_txen_ticks = (uint32_t)c;
            }
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
                bleadv_fmt_hex(hx, tiku_radio_arch_dbg_scanreq_pkt[k], 16, 0);
                SHELL_PRINTF("  heard: %s\n", hx);
            }
        }
        return;
    }
    if (strcmp(argv[1], "connprobe") == 0) {
        unsigned s = 20u;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 120) { s = (unsigned)v; }
        }
        if (argc >= 4) {                          /* sweep the turnaround   */
            long t = strtol(argv[3], (char **)0, 10);
            if (t >= 100 && t <= 250) {
                tiku_radio_arch_connadv_tifs_cfg = (uint32_t)t;
            }
        }
        if (argc >= 5) {                          /* ticks to reply's TXEN  */
            long c = strtol(argv[4], (char **)0, 10);
            if (c >= 20 && c <= 2000) {
                tiku_radio_arch_connadv_txen_ticks = (uint32_t)c;
            }
        }
        tiku_radio_arch_connadv_pdu_type = 0x00u;
        if (argc >= 6) {                          /* the PDU type to send   */
            if (strcmp(argv[5], "nonconn") == 0) {
                tiku_radio_arch_connadv_pdu_type = 0x02u;
            } else if (strcmp(argv[5], "scan") == 0) {
                tiku_radio_arch_connadv_pdu_type = 0x06u;
            }
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

    /* Demo form: a 100 ms background beacon that stops itself after ~secs.
     * The command returns at once: tiku_ble_adv sends the bursts in the
     * background, and the auto-stop is a timer callback that runs while the
     * shell idles at the prompt. */
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
