/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_bleadv.c - nRF54L15 BLE advertising (beacon) bring-up.
 *
 * Opt-in (TIKU_SHELL_CMD_BLEADV=1 via EXTRA_CFLAGS), nRF54L15 only.  Distinct
 * from the Ambiq EM9305 "ble" command; this drives the on-die 2.4 GHz RADIO
 * directly (tiku_radio_arch).
 *
 *   bleadv <name>   beacon ADV_NONCONN_IND for ~3 s: Flags + Complete Local
 *                   Name + a 2-byte manufacturer marker 'TK', on 37/38/39.
 *                   A host scanner (bluetoothctl) sees <name>; the TikuBench
 *                   ble-adv suite asserts exactly that.
 *   bleadv scan [n] diagnostic RX probe, n rounds (default 120)
 *   bleadv dbg      dump silicon rev + clock/radio state (errata gating)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_bleadv.h"

#if TIKU_SHELL_CMD_BLEADV

#include <kernel/shell/tiku_shell_io.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <arch/nordic/tiku_timer_arch.h>   /* TIKU_CLOCK_ARCH_SECOND before clock.h */
#include <kernel/timers/tiku_clock.h>
#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_device_select.h>  /* NRF_CLOCK_S / NRF_RADIO_S readbacks */
#include <stdlib.h>
#include <string.h>

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
    SHELL_PRINTF("       timing=%lx phyendtxdelay=%lx tifs=%lx feconfig=%lx\n",
                 (unsigned long)NRF_RADIO_S->TIMING,
                 (unsigned long)NRF_RADIO_S->PHYENDTXDELAY,
                 (unsigned long)NRF_RADIO_S->TIFS,
                 (unsigned long)NRF_RADIO_S->FECONFIG);
    SHELL_PRINTF("       err40[7AC]=%lx\n",
                 *(volatile unsigned long *)0x5008A7ACul);
    /* FICR trim census: how many factory trim pairs exist, and how many
     * target the RADIO block (0x5008Axxx) -- an untrimmed PA would explain
     * a full-length TX nobody hears. */
    {
        unsigned total = 0u, radio = 0u;
        uint32_t i;
        for (i = 0u; i < FICR_TRIMCNF_MaxCount; i++) {
            uint32_t a = NRF_FICR_NS->TRIMCNF[i].ADDR;
            if (a == 0xFFFFFFFFul || a == 0x00000000ul) {
                break;
            }
            total++;
            if ((a & 0xFFFFF000ul) == 0x5008A000ul ||
                (a & 0xFFFFF000ul) == 0x5008B000ul) {
                radio++;
            }
        }
        SHELL_PRINTF("FICR : trims=%u radio-trims=%u\n", total, radio);
    }
}

/* Random static BLE address: top two bits of the MSB = 11 (Core spec); the
 * rest from the FICR device ID so each board is distinct. */
static void bleadv_random_addr(uint8_t addr[6])
{
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;
}

/* SHELL_PRINTF has no %02X, so format the address (host order: MSB first)
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

void tiku_shell_cmd_bleadv(int argc, char **argv)
{
    static uint8_t pdu[40] __attribute__((aligned(4)));   /* radio EasyDMA src */
    uint8_t addr[6], ad[31];
    uint8_t nlen, adlen, total;
    char addrstr[18];
    const char *name;
    tiku_clock_time_t t0;
    unsigned secs = 3u;

    if (argc < 2) {
        SHELL_PRINTF("usage: bleadv <name> [seconds]  |  bleadv scan [rounds]"
                     "  |  bleadv dbg\n");
        return;
    }
    if (strcmp(argv[1], "dbg") == 0) {
        bleadv_dbg();
        return;
    }
    /* Diagnostic RX: prove the shared link config by hearing the room. */
    if (strcmp(argv[1], "scan") == 0) {
        uint32_t addr_evts = 0u, crcok = 0u;
        uint32_t rounds = 120u;
        int got;
        if (argc >= 3) {
            long v = strtol(argv[2], (char **)0, 10);
            if (v > 0 && v <= 100000) { rounds = (uint32_t)v; }
        }
        SHELL_PRINTF("listening on 37/38/39, %u rounds (same link config "
                     "as TX)...\n", (unsigned)rounds);
        tiku_radio_arch_init();
        got = tiku_radio_arch_rx_probe(addr, &addr_evts, &crcok, rounds);
        tiku_watchdog_kick();
        SHELL_PRINTF("  addr-match=%u crc-ok=%u\n",
                     (unsigned)addr_evts, (unsigned)crcok);
        if (got) {
            bleadv_fmt_addr(addrstr, addr);
            SHELL_PRINTF("  heard AdvA %s\n", addrstr);
        }
        SHELL_PRINTF(SH_GREEN "done" SH_RST "\n");
        return;
    }
    name = argv[1];
    if (argc >= 3) {
        long v = strtol(argv[2], (char **)0, 10);
        if (v > 0 && v <= 120) { secs = (unsigned)v; }
    }
    nlen = (uint8_t)strlen(name);
    if (nlen > 20u) {
        nlen = 20u;
    }

    adlen = 0u;
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;   /* Flags */
    ad[adlen++] = (uint8_t)(1u + nlen);
    ad[adlen++] = 0x09u;                                            /* name  */
    memcpy(&ad[adlen], name, nlen); adlen = (uint8_t)(adlen + nlen);
    ad[adlen++] = 0x03u; ad[adlen++] = 0xFFu; ad[adlen++] = 'T';
    ad[adlen++] = 'K';                                             /* mfr   */

    bleadv_random_addr(addr);
    total = tiku_radio_arch_adv_build(pdu, addr, ad, adlen);

    tiku_radio_arch_init();
    bleadv_fmt_addr(addrstr, addr);
    SHELL_PRINTF("beaconing '%s' (%u B PDU) as %s on 37/38/39 for ~%u s...\n",
                 name, (unsigned)total, addrstr, secs);

    t0 = tiku_clock_time();
    while ((tiku_clock_time_t)(tiku_clock_time() - t0)
           < (tiku_clock_time_t)(secs * TIKU_CLOCK_SECOND)) {
        tiku_clock_time_t b = tiku_clock_time();
        tiku_radio_arch_adv_send(pdu, total);
        tiku_watchdog_kick();
        while ((tiku_clock_time_t)(tiku_clock_time() - b)
               < (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 50u)) {
            /* pace to ~20 ms between advertising events */
        }
    }
    SHELL_PRINTF("  radio: ready=%lu disabled=%lu state=%lu spin=%lu "
                 "ru=%lu tx=%lu\n",
                 (unsigned long)tiku_radio_arch_dbg_ready,
                 (unsigned long)tiku_radio_arch_dbg_disabled,
                 (unsigned long)tiku_radio_arch_dbg_state,
                 (unsigned long)tiku_radio_arch_dbg_spin,
                 (unsigned long)tiku_radio_arch_dbg_ru_iters,
                 (unsigned long)tiku_radio_arch_dbg_tx_iters);
    SHELL_PRINTF(SH_GREEN "done" SH_RST "\n");
}

#endif /* TIKU_SHELL_CMD_BLEADV */
