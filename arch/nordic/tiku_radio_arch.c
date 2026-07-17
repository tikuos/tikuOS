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
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND        */
#include <kernel/timers/tiku_clock.h>          /* wall-clock bound for the scan */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during the long scan     */
#include <string.h>

#define RADIO  NRF_RADIO_S

/* BLE 1M legacy-advertising PHY/link constants (Core spec, all ours to set). */
#define BLE_ADV_ACCESS_BASE0   0x89BED600UL   /* access addr 0x8E89BED6:      */
#define BLE_ADV_ACCESS_PREFIX0 0x0000008EUL   /*   PREFIX||BASE, BALEN=3      */
#define BLE_ADV_CRC_POLY       0x0100065BUL   /* x^24+x^10+x^9+x^6+x^4+x^3+x+1 */
#define BLE_ADV_CRC_INIT       0x00555555UL   /* advertising CRC init         */
#define BLE_WHITE_POLY         0x00890000UL   /* DATAWHITE POLY field (0x89)  */

/* TXPOWER is an ENUMERATED code, not signed dBm (+8 dBm = 0x03F, 0 dBm =
 * 0x018 -- the bring-up lesson).  The MDK's dBm-named codes are the only
 * legal contract; the table below is identical across L15/LM20A/B.
 * Discrete steps only: an unlisted dBm request is rejected, never rounded
 * -- on a microwatt budget an explicit power request that cannot be
 * honoured exactly should fail loudly. */
static const struct {
    int8_t   dbm;
    uint16_t code;
} radio_txpower_map[] = {
    {   8, RADIO_TXPOWER_TXPOWER_Pos8dBm  },
    {   7, RADIO_TXPOWER_TXPOWER_Pos7dBm  },
    {   6, RADIO_TXPOWER_TXPOWER_Pos6dBm  },
    {   5, RADIO_TXPOWER_TXPOWER_Pos5dBm  },
    {   4, RADIO_TXPOWER_TXPOWER_Pos4dBm  },
    {   3, RADIO_TXPOWER_TXPOWER_Pos3dBm  },
    {   2, RADIO_TXPOWER_TXPOWER_Pos2dBm  },
    {   1, RADIO_TXPOWER_TXPOWER_Pos1dBm  },
    {   0, RADIO_TXPOWER_TXPOWER_0dBm     },
    {  -1, RADIO_TXPOWER_TXPOWER_Neg1dBm  },
    {  -2, RADIO_TXPOWER_TXPOWER_Neg2dBm  },
    {  -3, RADIO_TXPOWER_TXPOWER_Neg3dBm  },
    {  -4, RADIO_TXPOWER_TXPOWER_Neg4dBm  },
    {  -5, RADIO_TXPOWER_TXPOWER_Neg5dBm  },
    {  -6, RADIO_TXPOWER_TXPOWER_Neg6dBm  },
    {  -7, RADIO_TXPOWER_TXPOWER_Neg7dBm  },
    {  -8, RADIO_TXPOWER_TXPOWER_Neg8dBm  },
    {  -9, RADIO_TXPOWER_TXPOWER_Neg9dBm  },
    { -10, RADIO_TXPOWER_TXPOWER_Neg10dBm },
    { -12, RADIO_TXPOWER_TXPOWER_Neg12dBm },
    { -14, RADIO_TXPOWER_TXPOWER_Neg14dBm },
    { -16, RADIO_TXPOWER_TXPOWER_Neg16dBm },
    { -18, RADIO_TXPOWER_TXPOWER_Neg18dBm },
    { -20, RADIO_TXPOWER_TXPOWER_Neg20dBm },
    { -22, RADIO_TXPOWER_TXPOWER_Neg22dBm },
    { -28, RADIO_TXPOWER_TXPOWER_Neg28dBm },
    { -40, RADIO_TXPOWER_TXPOWER_Neg40dBm },
    { -46, RADIO_TXPOWER_TXPOWER_Neg46dBm },
};

/* Current setting: default +8 dBm (the strongest), applied at init and
 * re-applied on every set_txpower once the radio has been configured. */
static uint32_t radio_txpower_code = RADIO_TXPOWER_TXPOWER_Pos8dBm;
static int8_t   radio_txpower_dbm  = 8;
static uint8_t  radio_arch_inited;

/* The three primary advertising channels: RF frequency offset (2400+f MHz)
 * and the BLE logical channel index used for the whitening IV. */
static const uint8_t adv_freq[3] = { 2u, 26u, 80u };   /* ch37/38/39 = 2402/2426/2480 */
static const uint8_t adv_index[3] = { 37u, 38u, 39u };

/* Erratum-20 bracket: hold the MCU domain in Constant Latency for the
 * duration of any radio operation, then release to Low Power.
 *
 * For a duty-cycled beacon the facade additionally holds Constant Latency
 * across the whole session (tiku_radio_arch_constlat_hold): the sleeps
 * BETWEEN bursts then happen in constant-latency mode, per the erratum's
 * intent.  Note the hold alone is NOT what makes post-sleep bursts
 * decodable -- that is radio_hfclk_kick() below -- but it is the
 * documented erratum-20 discipline and stays.  While held, the per-burst
 * exit is a no-op. */
static uint8_t radio_constlat_held;

static void radio_constlat_enter(void)
{
    NRF_POWER_S->TASKS_CONSTLAT = 1u;
}

static void radio_constlat_exit(void)
{
    if (!radio_constlat_held) {
        NRF_POWER_S->TASKS_LOWPWR = 1u;
    }
}

void tiku_radio_arch_constlat_hold(int on)
{
    radio_constlat_held = (uint8_t)(on != 0);
    if (on) {
        NRF_POWER_S->TASKS_CONSTLAT = 1u;
    } else {
        NRF_POWER_S->TASKS_LOWPWR = 1u;
    }
}

/* XO/PLL observability only -- deliberately NO runtime clock manipulation.
 *
 * Post-mortem of the bring-up detours (all hardware-measured, kept here so
 * nobody re-walks them): runtime TASKS_XOSTART without PLLSTART wedges the
 * device (erratum 39); per-burst TASKS_PLLSTART opens a relock transient
 * the burst then flies inside (host silence); session-start
 * PLLSTART+XOTUNE made later sessions unreliable.  EVENTS_XOSTARTED never
 * re-fires across tickless sleeps (the XO does not park), XO.STAT stays
 * Running, and the radio's own READY fires in every failing case -- there
 * is no PASSIVE signal for the missing prerequisite.  The active fix that
 * finally proved out is the per-burst HF clock REQUEST in
 * radio_hfclk_kick() below (found via a UARTE-TX-byte discriminator:
 * one putc before each burst healed TX, because UARTE requests its
 * HFXO-derived reference; busy-waiting up to 11 ms did not).  This
 * observer only feeds the dbg counters. */
uint32_t tiku_radio_arch_dbg_xo_stat, tiku_radio_arch_dbg_xo_wait;
uint32_t tiku_radio_arch_dbg_xo_restarts;

static void radio_xo_observe(void)
{
    uint32_t stat = NRF_CLOCK_S->XO.STAT;

    tiku_radio_arch_dbg_xo_stat = stat;
    if (NRF_CLOCK_S->EVENTS_XOSTARTED != 0u || (stat & (1ul << 16)) == 0u) {
        tiku_radio_arch_dbg_xo_restarts++;     /* would be a NEW hardware fact */
    }
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

    RADIO->TXPOWER = radio_txpower_code;        /* enum code, not dBm!         */

    /* Auto-sequence: ramp-up -> READY -> (start) -> tx -> PHYEND -> (disable). */
    RADIO->SHORTS = (1u << 0) | (1u << 19);     /* READY_START | PHYEND_DISABLE */
    radio_arch_inited = 1u;
}

int tiku_radio_arch_set_txpower(int8_t dbm)
{
    uint8_t i;

    for (i = 0u; i < sizeof(radio_txpower_map) / sizeof(radio_txpower_map[0]);
         i++) {
        if (radio_txpower_map[i].dbm == dbm) {
            radio_txpower_dbm  = dbm;
            radio_txpower_code = radio_txpower_map[i].code;
            if (radio_arch_inited) {
                /* Applied at the next ramp-up; safe between bursts.  The
                 * CALLER guarantees the RADIO answers on the secure alias
                 * -- while the FLPR owns it (beacon offload) this write
                 * would be a precise bus fault, so the facade reclaims
                 * the peripheral around it. */
                RADIO->TXPOWER = radio_txpower_code;
            }
            return 0;
        }
    }
    return -1;                                 /* not a silicon-legal step     */
}

int8_t tiku_radio_arch_txpower(void)
{
    return radio_txpower_dbm;
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

/* Per-burst HF clock kick (the productized form of a hardware finding):
 * a burst fired after tickless sleep is undecodable unless a PERIPHERAL
 * clock request precedes it.  One console-UARTE TX byte before each burst
 * healed TX on hardware; CPU busy-waits up to 11 ms and every CLOCK-task
 * combination (PLLSTART/XOSTART/XOTUNE, per-burst and per-session) did
 * NOT.  Whatever the UARTE transaction requests from the clock arbiter is
 * the radio's missing prerequisite, so reproduce exactly that -- minus the
 * console pollution: a 1-byte TX DMA through the unused UARTE21 with its
 * pins left disconnected (PSEL reset = 0xFFFFFFFF, nothing is driven).
 * At 1 MBd the transaction is ~10 us; the bounded wait covers a stuck
 * ENDTX (then dbg_xo_wait pins at the bound and the burst proceeds --
 * degraded, not wedged). */
static void radio_hfclk_kick(void)
{
    static uint8_t kick_bytes[16];
    NRF_UARTE_Type *u = NRF_UARTE21_S;

    if (u->ENABLE == 0u) {
        u->BAUDRATE = 0x01D60000u;             /* 115200                   */
        u->ENABLE   = 8u;                      /* UARTE enable code        */
    }
    /* The request must be HELD ACROSS THE BURST, not just pulsed: a 1-byte
     * kick at 1 MBd (~10 us, released before the radio even finishes its
     * ramp) measurably does NOT heal TX, while the console putc's ~87 us
     * byte partially did.  16 bytes at 115200 spans ~1.6 ms -- longer than
     * the whole 3-channel burst -- and runs CONCURRENTLY (no wait); the
     * transfer self-completes after the burst and the next kick re-arms. */
    u->EVENTS_DMA.TX.END   = 0u;
    u->DMA.TX.PTR          = (uint32_t)kick_bytes;
    u->DMA.TX.MAXCNT       = sizeof(kick_bytes);
    u->TASKS_DMA.TX.START  = 1u;
    tiku_radio_arch_dbg_xo_wait = 0u;
}

void tiku_radio_arch_adv_send(const uint8_t *pdu, uint8_t pdu_len)
{
    uint8_t c;
    (void)pdu_len;                             /* length is byte[1] of the PDU */
    radio_constlat_enter();                    /* erratum 20: before any TXEN  */
    radio_xo_observe();                        /* clock-tree dbg counters only */
    radio_hfclk_kick();                        /* peripheral clock request     */
    for (c = 0; c < 3u; c++) {
        adv_tx_one(c, pdu);
    }
    radio_constlat_exit();                     /* radio disabled again         */
}

/**
 * @brief Observer scan: listen on the advertising channels with the exact
 *        same link config as TX, invoking @p cb per CRC-OK packet.
 *
 * The engine round-robins 37/38/39 in bounded listen windows until @p ms
 * milliseconds of wall clock have elapsed (tiku_clock based).  RSSI is
 * latched per packet via the ADDRESS->RSSISTART short and reported in dBm.
 * Besides being the BLESCAN$/`bleadv scan` backend, this doubles as the
 * link-config oracle that cornered erratum 49 during bring-up: if it hears
 * the room, everything shared with TX (frequency, access address,
 * whitening, CRC) is proven, and a TX failure is TX-only.
 *
 * @param cb          Called per CRC-OK packet with the raw RAM buffer
 *                    ([S0][LEN][S1 slot][payload...] -- the erratum-49
 *                    S1INCL slot shifts payload to byte 3), total payload
 *                    length (the LEN byte), and RSSI in dBm.  May be NULL.
 * @param ud          Opaque context for @p cb.
 * @param ms          Scan duration in milliseconds (wall clock).
 * @param addr_evts   Optional: incremented per access-address match.
 * @param crcok_evts  Optional: incremented per CRC-OK packet.
 */
void tiku_radio_arch_scan(tiku_radio_arch_scan_cb_t cb, void *ud, uint32_t ms,
                          uint32_t *addr_evts, uint32_t *crcok_evts)
{
    static uint8_t rxbuf[48] __attribute__((aligned(4)));
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_clock_time_t span =
        (tiku_clock_time_t)((ms * (uint32_t)TIKU_CLOCK_SECOND) / 1000u);
    uint32_t r = 0u;
    uint8_t chan;

    if (span == 0u) {
        span = 1u;
    }

    radio_constlat_enter();                    /* erratum 20: before any RXEN  */
    radio_xo_observe();                        /* clock-tree dbg counters only */

    /* RX ramps via RXREADY (distinct from TX's READY); PHYEND is
     * end-of-packet for BLE; ADDRESS latches an RSSI sample. */
    RADIO->SHORTS = (1u << 0) | (1u << 4) | (1u << 18) | (1u << 19);

    while ((tiku_clock_time_t)(tiku_clock_time() - t0) < span) {
        uint32_t spin;
        if ((r & 0x0Fu) == 0u) {
            tiku_watchdog_kick();              /* scan blocks for seconds      */
        }
        chan = (uint8_t)(r % 3u);
        r++;
        RADIO->FREQUENCY = adv_freq[chan];
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
        RADIO->PACKETPTR = (uint32_t)rxbuf;

        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_ADDRESS  = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        (void)RADIO->EVENTS_DISABLED;
        RADIO->TASKS_RXEN = 1u;

        /* Bounded listen window (~10-20 ms), then rotate channels. */
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
        if (RADIO->EVENTS_ADDRESS != 0u && addr_evts != (uint32_t *)0) {
            (*addr_evts)++;
        }
        if (RADIO->EVENTS_CRCOK != 0u) {
            if (crcok_evts != (uint32_t *)0) {
                (*crcok_evts)++;
            }
            if (cb != (tiku_radio_arch_scan_cb_t)0) {
                /* RSSISAMPLE is the magnitude in -dBm (7-bit). */
                int8_t rssi =
                    (int8_t)(-(int)(RADIO->RSSISAMPLE & 0x7Fu));
                cb(rxbuf, rxbuf[1], rssi, ud);
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
