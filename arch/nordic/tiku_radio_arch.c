/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_radio_arch.c - nRF54L15 2.4 GHz RADIO: BLE legacy advertising (TX)
 *                     plus a diagnostic receiver probe.
 *
 * From-scratch BLE driver against the MDK register map (no SoftDevice, no
 * sdk-nrf).  Broadcaster only: it transmits a legacy ADV_NONCONN_IND PDU on
 * the three primary advertising channels (37/38/39).  Connections, scanning,
 * and 802.15.4 are separate phases.
 *
 * nRF54 vs nRF52 register facts that bit the bring-up (the RP2350 lesson --
 * never paste an older generation's offsets/encodings):
 *   - PACKETPTR moved to 0xED0 (was 0x504).
 *   - Whitening is DATAWHITE (POLY bits 25:16 + IV bits 8:0), not the nRF52
 *     DATAWHITEIV-only register; the reset value 0x00890040 already carries
 *     the BLE polynomial 0x89, so per-channel we only OR the IV = 0x40|index.
 *   - TXPOWER is an ENUMERATED code (+8 dBm = 0x03F), not signed dBm.
 *   - Split interrupt banks (INTENSET00/01/10/11); this path is POLLED.
 *   - PHYEND (not END) is the "last bit on air" event for BLE 1M.
 *   - RXADDRESSES resets to 0: RX matches nothing until logical address 0
 *     is explicitly enabled.
 *
 * Silicon errata this driver works around (nRF54L15 sheet 4503_401, all
 * present on Rev 1 AND Rev 2 -- each cost a day-class debug on hardware):
 *
 *   Erratum 49 ("First bits of on-air packet are not correct"): with
 *   S1LEN=0 the radio corrupts the leading PAYLOAD byte(s) on air, so every
 *   packet fails the receiver's CRC and is silently dropped, while the TX
 *   sequencer (READY/PHYEND/DISABLED, airtime) looks perfect.  Workaround
 *   (the errata's S1LEN==0 branch): include the *untransmitted* S1 RAM slot
 *   (PCNF0.S1INCL=Include) and duplicate the first payload byte into it --
 *   RAM layout [S0][LEN][S1=payload0][payload...], on-air format unchanged.
 *   Host-decode of the beacon flipped from 0% to reliable the moment this
 *   was applied; it is NOT optional.  Note the RX direction then also
 *   carries the S1 slot in RAM: received payload starts at buffer[3].
 *
 *   Erratum 20 ("RADIO payload is not transmitted"): if the MCU power
 *   domain sleeps around a radio operation the payload never leaves the PA
 *   (and an RX in that state can wedge the AHB -- the CPU's next RADIO
 *   register read hangs forever, indistinguishable from a crash).
 *   Mandatory workaround: POWER.TASKS_CONSTLAT before TASKS_TXEN/RXEN,
 *   POWER.TASKS_LOWPWR after disable.  Each burst/probe brackets itself so
 *   idle power between advertising events is unaffected.
 *
 *   Erratum 39 (PLLSTART before XOSTART) and the XOTUNED wait are handled
 *   at boot in tiku_cpu_freq_boot_arch.c; the SystemInit-parity trim/errata
 *   pokes live in tiku_crt_early.c.
 *
 * Sequence per channel, driven entirely by SHORTS so the CPU only polls one
 * event: set FREQUENCY + DATAWHITE + PACKETPTR, TASKS_TXEN; the chain
 * READY->START (SHORTS bit 0) transmits, PHYEND->DISABLE (SHORTS bit 19)
 * tears down, and we poll EVENTS_DISABLED.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK register types + NRF_RADIO_S */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during the long RX probe */
#include <string.h>

#define RADIO  NRF_RADIO_S

/* BLE 1M legacy-advertising PHY/link constants (Core spec, all ours to set). */
#define BLE_ADV_ACCESS_BASE0   0x89BED600UL   /* access addr 0x8E89BED6:      */
#define BLE_ADV_ACCESS_PREFIX0 0x0000008EUL   /*   PREFIX||BASE, BALEN=3      */
#define BLE_ADV_CRC_POLY       0x0100065BUL   /* x^24+x^10+x^9+x^6+x^4+x^3+x+1 */
#define BLE_ADV_CRC_INIT       0x00555555UL   /* advertising CRC init         */
#define BLE_WHITE_POLY         0x00890000UL   /* DATAWHITE POLY field (0x89)  */

/* TXPOWER enum code: +8 dBm, the strongest setting (0 dBm = 0x018). */
#define BLE_TXPOWER_POS8DBM    0x03FUL

/* The three primary advertising channels: RF frequency offset (2400+f MHz)
 * and the BLE logical channel index used for the whitening IV. */
static const uint8_t adv_freq[3] = { 2u, 26u, 80u };   /* ch37/38/39 = 2402/2426/2480 */
static const uint8_t adv_index[3] = { 37u, 38u, 39u };

/* Erratum-20 bracket: hold the MCU domain in Constant Latency for the
 * duration of any radio operation, then release to Low Power. */
static void radio_constlat_enter(void)
{
    NRF_POWER_S->TASKS_CONSTLAT = 1u;
}

static void radio_constlat_exit(void)
{
    NRF_POWER_S->TASKS_LOWPWR = 1u;
}

void tiku_radio_arch_init(void)
{
    /* Modulation + packet format (configured once; per-packet we only touch
     * FREQUENCY, DATAWHITE, PACKETPTR). */
    RADIO->MODE = 3u;                          /* Ble_1Mbit                   */

    /* PDU layout: 1-byte S0 (the PDU header), 8-bit LENGTH, S1LEN=0 but
     * S1INCL=Include -- the erratum-49 workaround RAM slot (see header
     * comment); 8-bit preamble.  MAXLEN 37, 3-byte base address,
     * little-endian, whitening on. */
    RADIO->PCNF0 = (8u << 0) | (1u << 8) | (0u << 16) |
                   (1u << 20) | (0u << 24);
    RADIO->PCNF1 = (37u << 0) | (0u << 8) | (3u << 16) |
                   (0u << 24) | (1u << 25);

    /* Access address 0x8E89BED6 on logical address 0 -- for TX (TXADDRESS
     * selects it) and RX (RXADDRESSES is a bitmask of enabled logical
     * addresses; it RESETS TO 0, so without this the receiver can never
     * address-match anything). */
    RADIO->BASE0       = BLE_ADV_ACCESS_BASE0;
    RADIO->PREFIX0     = BLE_ADV_ACCESS_PREFIX0;
    RADIO->TXADDRESS   = 0u;
    RADIO->RXADDRESSES = 1u;

    /* 24-bit CRC, computed over the PDU but NOT the access address. */
    RADIO->CRCCNF  = (3u << 0) | (1u << 8);    /* LEN=Three, SKIPADDR=Skip    */
    RADIO->CRCPOLY = BLE_ADV_CRC_POLY;
    RADIO->CRCINIT = BLE_ADV_CRC_INIT;

    RADIO->TXPOWER = BLE_TXPOWER_POS8DBM;       /* +8 dBm (enum, not 0!)       */

    /* Auto-sequence: ramp-up -> READY -> (start) -> tx -> PHYEND -> (disable). */
    RADIO->SHORTS = (1u << 0) | (1u << 19);     /* READY_START | PHYEND_DISABLE */
}

/**
 * @brief Transmit one PDU on a single advertising channel (blocking, polled).
 *
 * Every poll iteration also samples STATE and counts the iterations spent
 * in TXRU (0x9) and TX (0xB): dbg_tx_iters scaling linearly with the PDU
 * length is the on-die proof that the modulator ran for the whole frame
 * (verified against host decode during bring-up).
 */
static void adv_tx_one(uint8_t chan, const uint8_t *pdu)
{
    uint32_t spin, ru = 0u, tx = 0u;

    RADIO->FREQUENCY = adv_freq[chan];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
    RADIO->PACKETPTR = (uint32_t)pdu;

    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_READY    = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_TXEN = 1u;

    for (spin = 0; spin < 1000000u; spin++) {
        uint32_t st = RADIO->STATE;
        if (st == 0x9u) {
            ru++;
        } else if (st == 0xBu) {
            tx++;
        }
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    tiku_radio_arch_dbg_ready    = RADIO->EVENTS_READY;
    tiku_radio_arch_dbg_disabled = RADIO->EVENTS_DISABLED;
    tiku_radio_arch_dbg_state    = RADIO->STATE;
    tiku_radio_arch_dbg_spin     = spin;
    tiku_radio_arch_dbg_ru_iters = ru;
    tiku_radio_arch_dbg_tx_iters = tx;
}

uint32_t tiku_radio_arch_dbg_ready, tiku_radio_arch_dbg_disabled;
uint32_t tiku_radio_arch_dbg_state, tiku_radio_arch_dbg_spin;
uint32_t tiku_radio_arch_dbg_ru_iters, tiku_radio_arch_dbg_tx_iters;

void tiku_radio_arch_adv_send(const uint8_t *pdu, uint8_t pdu_len)
{
    uint8_t c;
    (void)pdu_len;                             /* length is byte[1] of the PDU */
    radio_constlat_enter();                    /* erratum 20: before any TXEN  */
    for (c = 0; c < 3u; c++) {
        adv_tx_one(c, pdu);
    }
    radio_constlat_exit();                     /* radio disabled again         */
}

/**
 * @brief Diagnostic RX probe: listen on the advertising channels with the
 *        exact same link config as TX and report what the PHY locks onto.
 *
 * A real BLE advertiser in range makes EVENTS_ADDRESS fire (access-address +
 * preamble config correct) and EVENTS_CRCOK fire (whitening + CRC correct).
 * This isolates a "transmits but nobody decodes" failure: if RX hears the
 * room, the shared link config is proven and the bug is TX-only -- exactly
 * how erratum 49 was cornered during bring-up.
 *
 * @param out_adva   6-byte buffer; receives the AdvA of the first CRC-OK PDU
 * @param addr_evts  incremented per access-address match seen
 * @param crcok_evts incremented per CRC-OK packet seen
 * @return 1 if at least one CRC-OK packet was captured, else 0
 */
int tiku_radio_arch_rx_probe(uint8_t *out_adva, uint32_t *addr_evts,
                             uint32_t *crcok_evts, uint32_t rounds)
{
    static uint8_t rxbuf[40] __attribute__((aligned(4)));
    uint32_t r, got = 0u;
    uint8_t chan = 0u;

    radio_constlat_enter();                    /* erratum 20: before any RXEN  */

    /* RX ramps via RXREADY (distinct from TX's READY); PHYEND is end-of-packet
     * for BLE.  Cover both ready events, then disable at PHYEND. */
    RADIO->SHORTS = (1u << 0) | (1u << 18) | (1u << 19);

    for (r = 0; r < rounds; r++) {
        uint32_t spin;
        if ((r & 0x0Fu) == 0u) {
            tiku_watchdog_kick();              /* probe blocks for seconds     */
        }
        chan = (uint8_t)(r % 3u);
        RADIO->FREQUENCY = adv_freq[chan];
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
        RADIO->PACKETPTR = (uint32_t)rxbuf;

        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_END      = 0u;
        RADIO->EVENTS_ADDRESS  = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        (void)RADIO->EVENTS_DISABLED;
        RADIO->TASKS_RXEN = 1u;

        /* Bounded listen window per round; the whole probe stays well under
         * the WDT with the periodic kicks above. */
        for (spin = 0; spin < 120000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_DISABLED == 0u) {
            RADIO->TASKS_DISABLE = 1u;
            for (spin = 0; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        }
        if (RADIO->EVENTS_ADDRESS != 0u) {
            (*addr_evts)++;
        }
        if (RADIO->EVENTS_CRCOK != 0u) {
            (*crcok_evts)++;
            if (!got) {
                /* rxbuf = [S0][LEN][S1 slot][AdvA0..5][...] -- the S1INCL
                 * erratum-49 slot shifts received payload to byte 3. */
                out_adva[0] = rxbuf[3]; out_adva[1] = rxbuf[4];
                out_adva[2] = rxbuf[5]; out_adva[3] = rxbuf[6];
                out_adva[4] = rxbuf[7]; out_adva[5] = rxbuf[8];
                got = 1u;
            }
        }
    }
    /* Leave the radio disabled + TX shorts restored for the next user. */
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    for (r = 0; r < 40000u; r++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    radio_constlat_exit();
    return (int)got;
}

uint8_t tiku_radio_arch_adv_build(uint8_t *pdu, const uint8_t *addr,
                                  const uint8_t *ad, uint8_t ad_len)
{
    uint8_t plen;
    if (ad_len > 31u) {
        ad_len = 31u;                          /* legacy adv AD cap           */
    }
    plen = (uint8_t)(6u + ad_len);             /* AdvA(6) + AD                 */

    pdu[0] = 0x42u;                            /* ADV_NONCONN_IND, TxAdd=random */
    pdu[1] = plen;                             /* LENGTH                        */
    pdu[2] = addr[0];                          /* erratum-49 S1 slot: duplicate
                                                * of the first payload byte    */
    memcpy(&pdu[3], addr, 6u);                 /* AdvA (little-endian on air)   */
    if (ad_len) {
        memcpy(&pdu[9], ad, ad_len);
    }
    return (uint8_t)(3u + plen);               /* total bytes in the buffer     */
}
