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
 *   - Split interrupt banks (INTENSET00/01/10/11).  TX is POLLED (a 1.3 ms
 *     fire-and-forget burst needs no interrupt); the observer/scan path is
 *     IRQ-driven since R6.1 (RADIO_0 = IRQn 138, bank 00, priority 4 --
 *     below htimer/console/tick so scanning can never cost console bytes).
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
#include <arch/nordic/tiku_nordic_core.h>      /* NVIC + WFE (IRQ scan, R6.1)   */
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

/* The enumerated TXPOWER code for the current setting, so the 15.4 PHY (a
 * separate arch file) programs the SAME power the BLE path + /sys/radio/
 * txpower use -- one knob for the shared radio. */
uint32_t tiku_radio_arch_txpower_code(void)
{
    return radio_txpower_code;
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

/* Public entry to the HFCLK kick, so the 15.4 PHY (a separate arch file
 * sharing this RADIO) can start the HFXO the same erratum-safe way before
 * its own TXEN/RXEN and re-arm it across a long listen. */
void tiku_radio_arch_hfclk_kick(void)
{
    radio_hfclk_kick();
}

/* Decode the live RADIO.MODE for /sys/radio/mode -- honest current PHY,
 * not a compile-time string (reads "ieee802154" while the 15.4 PHY owns
 * the radio, "ble-1m" at rest). */
const char *tiku_radio_arch_mode_str(void)
{
    switch (RADIO->MODE) {
    case RADIO_MODE_MODE_Ble_1Mbit:            return "ble-1m";
    case RADIO_MODE_MODE_Ble_2Mbit:            return "ble-2m";
    case RADIO_MODE_MODE_Ieee802154_250Kbit:   return "ieee802154";
    default:                                   return "other";
    }
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
/*---------------------------------------------------------------------------*/
/* IRQ-driven observer engine (R6.1)                                         */
/*---------------------------------------------------------------------------*/
/*
 * The polled engine burned the CPU for the whole scan (a multi-second
 * 100% spin) and its listen windows were spin-count-bounded.  Now the
 * ISR owns the per-packet work: on DISABLED (the PHYEND->DISABLE short
 * fires it per received packet) it captures the packet + RSSI into a
 * small SPSC ring and re-arms RX on the next advertising channel.  The
 * blocking API is unchanged -- the caller's context drains the ring and
 * WFEs between packets, waking on any interrupt (radio, tick, console).
 * Idle channels never fire DISABLED, so the drain loop force-rotates a
 * silent channel after ~2 ticks (the ISR's hop path handles the rest);
 * R6.2 replaces that coarse rotation with TIMER-gated windows.
 *
 * Coexistence rule (phase-6 risk, now enforced in code): RADIO IRQ
 * priority is 4 -- strictly below the htimer (1), console UARTE (2) and
 * tick/GPIOTE (3), so a scan can never cost console bytes; the
 * /dev/uart/overruns counter is the standing detector.
 */

#define TIKU_NORDIC_IRQ_RADIO   138    /* RADIO_0 = periph 0x8A @ 0x5008A000 */
#define RADIO_INTEN00_DISABLED  (1u << 8)

/* R6.2: hardware listen windows.  TIMER10 (free in GRTC-tick builds) +
 * DPPIC10 channel 0, all inside the radio power domain: COMPARE[0]
 * publishes to the channel, RADIO TASKS_DISABLE subscribes, so a silent
 * channel closes after RADIO_SCAN_WINDOW_US with ZERO CPU involvement --
 * the resulting DISABLED IRQ is the same hop path as a packet end.  The
 * drain loop keeps a coarse-tick rotation as a COUNTED safety net
 * (dbg_win_forced): with the hardware window alive it must read 0.
 * The COMPARE0->STOP short makes each window one-shot; every channel
 * arm restarts the timer.  In -DTIKU_NORDIC_TICK_TIMER10 builds the
 * timer IS the kernel tick, so the radio falls back to the coarse
 * rotation alone. */
#if defined(TIKU_NORDIC_TICK_TIMER10)
#define RADIO_HW_WINDOW 0
#else
#define RADIO_HW_WINDOW 1
#define RADIO_SCAN_WINDOW_US  16000u    /* per-channel listen window       */
#define RADIO_DPPI_CH_WINDOW  0u        /* DPPIC10 channel (no other user) */
#endif

uint32_t tiku_radio_arch_dbg_win_hw, tiku_radio_arch_dbg_win_forced;

#if RADIO_HW_WINDOW
static void radio_window_start(void)
{
    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER10_S->TASKS_START = 1u;
}

static void radio_window_wire(void)
{
    NRF_TIMER10_S->TASKS_STOP = 1u;
    NRF_TIMER10_S->MODE       = 0u;                    /* timer            */
    NRF_TIMER10_S->BITMODE    = 3u;                    /* 32-bit           */
    NRF_TIMER10_S->PRESCALER  = 4u;                    /* 16 MHz/16 = 1 MHz */
    NRF_TIMER10_S->CC[0]      = RADIO_SCAN_WINDOW_US;
    NRF_TIMER10_S->SHORTS     = (1u << 8);             /* COMPARE0 -> STOP */
    NRF_TIMER10_S->PUBLISH_COMPARE[0] =
        RADIO_DPPI_CH_WINDOW | (1u << 31);
    RADIO->SUBSCRIBE_DISABLE = RADIO_DPPI_CH_WINDOW | (1u << 31);
    NRF_DPPIC10_S->CHENSET   = (1u << RADIO_DPPI_CH_WINDOW);
}

/* MUST run at scan teardown: a live SUBSCRIBE_DISABLE would let a stale
 * window later kill a TX burst mid-air. */
static void radio_window_unwire(void)
{
    NRF_TIMER10_S->TASKS_STOP = 1u;
    NRF_TIMER10_S->PUBLISH_COMPARE[0] = 0u;
    RADIO->SUBSCRIBE_DISABLE = 0u;
    NRF_DPPIC10_S->CHENCLR = (1u << RADIO_DPPI_CH_WINDOW);
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
}
#endif /* RADIO_HW_WINDOW */

#define RADIO_SCAN_RING  8u
struct radio_scan_pkt {
    uint8_t buf[48];                    /* [S0][LEN][S1 slot][payload...]  */
    int8_t  rssi;
};
static struct radio_scan_pkt scan_ring[RADIO_SCAN_RING];
static volatile uint8_t  scan_head;     /* ISR produces                    */
static volatile uint8_t  scan_tail;     /* drain loop consumes             */
static volatile uint8_t  scan_active;   /* ISR may re-arm while set        */
static volatile uint8_t  scan_chan;
static volatile uint32_t scan_isr_count;
static volatile uint32_t scan_addr_evts, scan_crcok_evts;
static uint8_t scan_rxbuf[48] __attribute__((aligned(4)));

/* Program the current channel and start RX (arm path + ISR hop path). */
static void radio_scan_arm_channel(void)
{
    RADIO->FREQUENCY = adv_freq[scan_chan];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[scan_chan]);
    RADIO->PACKETPTR = (uint32_t)scan_rxbuf;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_RXEN = 1u;
#if RADIO_HW_WINDOW
    radio_window_start();               /* fresh one-shot listen window    */
#endif
}

/* RADIO_0 ISR: one DISABLED per packet end (or forced rotation).  Copy
 * BEFORE re-arming -- EasyDMA would overwrite scan_rxbuf.  RSSISAMPLE
 * must also be read here (latched per ADDRESS->RSSISTART; the next
 * packet overwrites it). */
void tiku_nordic_radio_isr(void)
{
    if (RADIO->EVENTS_DISABLED == 0u) {
        return;                         /* spurious (line shared w/ nothing) */
    }
    RADIO->EVENTS_DISABLED = 0u;
    scan_isr_count++;
#if RADIO_HW_WINDOW
    if (NRF_TIMER10_S->EVENTS_COMPARE[0] != 0u) {
        NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
        tiku_radio_arch_dbg_win_hw++;   /* hardware window closed this one */
    }
#endif
    if (RADIO->EVENTS_ADDRESS != 0u) {
        scan_addr_evts++;
    }
    if (RADIO->EVENTS_CRCOK != 0u) {
        uint8_t next = (uint8_t)((scan_head + 1u) % RADIO_SCAN_RING);
        scan_crcok_evts++;
        if (next != scan_tail) {        /* ring full: drop, keep listening  */
            uint8_t n = scan_rxbuf[1];
            if (n > 44u) {
                n = 44u;                /* bound to the ring entry          */
            }
            memcpy(scan_ring[scan_head].buf, scan_rxbuf, (size_t)(3u + n));
            scan_ring[scan_head].rssi =
                (int8_t)(-(int)(RADIO->RSSISAMPLE & 0x7Fu));
            scan_head = next;
        }
    }
    if (scan_active) {
        scan_chan = (uint8_t)((scan_chan + 1u) % 3u);
        radio_scan_arm_channel();
    }
}

/* Safety-net rotation cadence: primary in fallback-tick builds (2 ticks);
 * with the hardware window it is a counted anomaly detector only (4 ticks
 * >> the 16 ms window -- it must never fire). */
#define RADIO_SCAN_ROT_TICKS  ((tiku_clock_time_t)(RADIO_HW_WINDOW ? 4u : 2u))
static tiku_clock_time_t scan_rot_wdl;
static uint32_t          scan_rot_seen;

/* Arm/disarm the RX engine WITHOUT touching Constant Latency or the
 * packet ring -- the shared core of start/stop AND pause/resume (R7.5).
 * A beacon sharing the radio (time-division) borrows it by disarm ->
 * burst -> arm, and the ring MUST survive so packets queued before the
 * burst are still delivered after it. */
static void radio_scan_arm(void)
{
    /* RX ramps via RXREADY (distinct from TX's READY); PHYEND is
     * end-of-packet for BLE; ADDRESS latches an RSSI sample. */
    RADIO->SHORTS = (1u << 0) | (1u << 4) | (1u << 18) | (1u << 19);
    scan_active = 1u;
    RADIO->INTENSET00 = RADIO_INTEN00_DISABLED;
    tiku_nordic_nvic_set_priority(TIKU_NORDIC_IRQ_RADIO, 4u);
    tiku_nordic_nvic_clear_pending(TIKU_NORDIC_IRQ_RADIO);
    tiku_nordic_nvic_enable(TIKU_NORDIC_IRQ_RADIO);
#if RADIO_HW_WINDOW
    radio_window_wire();                       /* TIMER10 -> DPPI -> DISABLE */
#endif
    radio_scan_arm_channel();
    scan_rot_seen = scan_isr_count;            /* don't false-rotate on resume */
    scan_rot_wdl = (tiku_clock_time_t)(tiku_clock_time()
                                       + RADIO_SCAN_ROT_TICKS);
}

static void radio_scan_disarm(void)
{
    uint32_t spin;

    scan_active = 0u;
    RADIO->INTENCLR00 = RADIO_INTEN00_DISABLED;
    tiku_nordic_nvic_disable(TIKU_NORDIC_IRQ_RADIO);
#if RADIO_HW_WINDOW
    radio_window_unwire();
#endif
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    for (spin = 0u; spin < 40000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    RADIO->SHORTS = (1u << 0) | (1u << 19);     /* TX-only contract restored */
}

void tiku_radio_arch_scan_start(void)
{
    radio_constlat_enter();                    /* erratum 20: before any RXEN  */
    radio_xo_observe();                        /* clock-tree dbg counters only */

    scan_head = 0u;
    scan_tail = 0u;
    scan_addr_evts = 0u;
    scan_crcok_evts = 0u;
    scan_isr_count = 0u;
    scan_chan = 0u;
    radio_scan_arm();
}

/* Time-division borrow (R7.5): hand the radio to a TX burst and take it
 * back, ring intact, Constant Latency untouched (the beacon session
 * holds it).  pause() leaves the radio idle with TX shorts -- exactly
 * what tiku_radio_arch_adv_send() expects. */
void tiku_radio_arch_scan_pause(void)
{
    radio_scan_disarm();
}

void tiku_radio_arch_scan_resume(void)
{
    radio_scan_arm();
}

uint8_t tiku_radio_arch_scan_service(tiku_radio_arch_scan_cb_t cb, void *ud)
{
    uint8_t delivered = 0u;

    /* Drain everything the ISR queued.  SPSC: tail is ours, head is the
     * ISR's; volatile ordering is sufficient on this single core. */
    while (scan_tail != scan_head) {
        struct radio_scan_pkt *p = &scan_ring[scan_tail];
        if (cb != (tiku_radio_arch_scan_cb_t)0) {
            cb(p->buf, p->buf[1], p->rssi, ud);
        }
        scan_tail = (uint8_t)((scan_tail + 1u) % RADIO_SCAN_RING);
        delivered++;
    }

    /* Safety-net rotation (skipped once the engine is disarmed -- a
     * post-stop service call only drains stragglers). */
    if (scan_active) {
        tiku_clock_time_t now = tiku_clock_time();
        if (scan_isr_count != scan_rot_seen) {         /* traffic: alive   */
            scan_rot_seen = scan_isr_count;
            scan_rot_wdl = (tiku_clock_time_t)(now + RADIO_SCAN_ROT_TICKS);
        } else if (TIKU_CLOCK_LT(scan_rot_wdl, now)) {
            tiku_radio_arch_dbg_win_forced++;
            RADIO->TASKS_DISABLE = 1u;                 /* silent: rotate   */
            scan_rot_wdl = (tiku_clock_time_t)(now + RADIO_SCAN_ROT_TICKS);
        }
    }
    return delivered;
}

void tiku_radio_arch_scan_stop(void)
{
    /* Disarm the RX engine (ISR out of the loop, radio idle, TX shorts
     * restored -- ring stragglers stay queued for one more service),
     * then release the per-op Constant Latency.  While a beacon session
     * holds CONSTLAT (combined mode, R7.5) the exit is suppressed. */
    radio_scan_disarm();
    radio_constlat_exit();
}

void tiku_radio_arch_scan(tiku_radio_arch_scan_cb_t cb, void *ud, uint32_t ms,
                          uint32_t *addr_evts, uint32_t *crcok_evts)
{
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_clock_time_t span =
        (tiku_clock_time_t)((ms * (uint32_t)TIKU_CLOCK_SECOND) / 1000u);

    if (span == 0u) {
        span = 1u;
    }

    tiku_radio_arch_scan_start();
    while ((tiku_clock_time_t)(tiku_clock_time() - t0) < span) {
        tiku_watchdog_kick();                  /* scan blocks for seconds  */
        (void)tiku_radio_arch_scan_service(cb, ud);
        tiku_nordic_wfe();                     /* sleep to the next IRQ    */
    }
    tiku_radio_arch_scan_stop();
    (void)tiku_radio_arch_scan_service(cb, ud);    /* teardown stragglers  */

    if (addr_evts != (uint32_t *)0) {
        *addr_evts += scan_addr_evts;
    }
    if (crcok_evts != (uint32_t *)0) {
        *crcok_evts += scan_crcok_evts;
    }
}

/*---------------------------------------------------------------------------*/
/* Multi-PHY probe (R8.1)                                                    */
/*---------------------------------------------------------------------------*/

/* Per-PHY MODE + PCNF0 preamble/coded fields (MDK encodings, verified in
 * nrf54l15_types.h -- never pasted from nRF52 folklore): PLEN@24
 * (8bit=0, 16bit=1, LongRange=3), CILEN@22, TERMLEN@29.  The base PCNF0
 * bits (LFLEN=8, S0LEN=1, erratum-49 S1INCL) stay identical across PHYs.
 * The BLE whitening/CRC config is PHY-independent (coded PHY whitens and
 * CRCs FEC block 2 in hardware). */
static const struct {
    uint8_t mode;                       /* RADIO_MODE_MODE_*               */
    uint8_t plen;                       /* PCNF0.PLEN                      */
    uint8_t cilen;                      /* PCNF0.CILEN                     */
    uint8_t termlen;                    /* PCNF0.TERMLEN                   */
} radio_phy_cfg[4] = {
    { 3u, 0u, 0u, 0u },                 /* 1M: 8-bit preamble              */
    { 4u, 1u, 0u, 0u },                 /* 2M: 16-bit preamble             */
    { 5u, 3u, 2u, 3u },                 /* Coded S=8 (125 kbps)            */
    { 6u, 3u, 2u, 3u },                 /* Coded S=2 (500 kbps)            */
};

#define RADIO_PCNF0_BASE  ((8u << 0) | (1u << 8) | (0u << 16) | (1u << 20))

static void radio_apply_phy(tiku_radio_arch_phy_t phy)
{
    RADIO->MODE  = radio_phy_cfg[phy].mode;
    RADIO->PCNF0 = RADIO_PCNF0_BASE |
                   ((uint32_t)radio_phy_cfg[phy].plen    << 24) |
                   ((uint32_t)radio_phy_cfg[phy].cilen   << 22) |
                   ((uint32_t)radio_phy_cfg[phy].termlen << 29);
}

int tiku_radio_arch_phy_tx_probe(tiku_radio_arch_phy_t phy,
                                 uint32_t iters[3])
{
    /* Fixed probe PDU: ADV_NONCONN_IND shape, 24-byte payload, erratum-49
     * S1 slot in place -- identical bits at every PHY so the iteration
     * ratios compare airtime and nothing else. */
    static uint8_t pdu[32] __attribute__((aligned(4)));
    uint8_t c;
    int rc = 0;

    pdu[0] = 0x42u;
    pdu[1] = 24u;
    for (c = 0u; c < 25u; c++) {
        pdu[2u + c] = (uint8_t)(0xA5u ^ c);    /* [2]=S1 dup of payload[0] */
    }

    radio_constlat_enter();                    /* erratum 20 bracket       */
    radio_xo_observe();
    radio_hfclk_kick();
    radio_apply_phy(phy);
    for (c = 0u; c < 3u; c++) {
        adv_tx_one(c, pdu);
        iters[c] = tiku_radio_arch_dbg_tx_iters;
        if (tiku_radio_arch_dbg_disabled == 0u) {
            rc = -1;                           /* spin cap hit: no PHYEND  */
        }
    }
    radio_apply_phy(TIKU_RADIO_PHY_1M);        /* beacon/scan contract     */
    radio_constlat_exit();
    return rc;
}

/*---------------------------------------------------------------------------*/
/* Two-board per-PHY link (R8.2): TX one prepared PDU / RX one packet at the  */
/* given PHY on advertising channel `chan` (0..2 = 37/38/39).  Reuses the adv */
/* access address + CRC (PHY-independent); only MODE/PCNF0 switch.  Neither   */
/* restores 1M -- the caller loops for a PER run, holds Constant Latency      */
/* across it (erratum 20), and re-inits afterwards for the beacon/scan        */
/* contract.  Together they measure link PER at 2M / Coded S8 / Coded S2.     */
/*---------------------------------------------------------------------------*/

static uint8_t phy_rxbuf[64] __attribute__((aligned(4)));

/* Distinctive access address for the R8.2 link so ambient BLE (all on the
 * shared adv AA 0x8E89BED6 at 1M) is address-rejected, not received. */
#define PHY_LINK_AA   0x71764129u

static void radio_phy_link_aa(void)
{
    RADIO->BASE0   = (PHY_LINK_AA & 0x00FFFFFFu) << 8;
    RADIO->PREFIX0 = (PHY_LINK_AA >> 24) & 0xFFu;
    RADIO->TXADDRESS   = 0u;
    RADIO->RXADDRESSES = 1u;
}

/* Force the RADIO to DISABLED so each op starts from a clean state -- a
 * prior run left mid-ramp/RXIDLE would make the next TXEN/RXEN misfire
 * (the run-to-run collapse this guards against). */
static void radio_phy_force_disable(void)
{
    uint32_t spin;
    RADIO->SHORTS = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    for (spin = 0u; spin < 400000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    RADIO->EVENTS_DISABLED = 0u;
}

int tiku_radio_arch_phy_tx(tiku_radio_arch_phy_t phy, uint8_t chan,
                           const uint8_t *pdu)
{
    uint32_t spin;

    if ((unsigned)phy > 3u) {
        return -1;
    }
    if (chan > 2u) {
        chan = 0u;
    }
    radio_hfclk_kick();
    radio_phy_force_disable();
    radio_apply_phy(phy);
    radio_phy_link_aa();
    RADIO->FREQUENCY = adv_freq[chan];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
    RADIO->PACKETPTR = (uint32_t)pdu;
    RADIO->SHORTS = (1u << 0) | (1u << 19);      /* READY_START|PHYEND_DISABLE */
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_TXEN = 1u;
    /* Big cap: coded S=8 airtime is ~8x 1M, far past adv_tx_one's window.
     * Kick the dog inside the wait so a stuck ramp can't reset the board. */
    for (spin = 0u; spin < 20000000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
        if ((spin & 0x3FFFFu) == 0u) {
            tiku_watchdog_kick();
        }
    }
    return (RADIO->EVENTS_DISABLED != 0u) ? 0 : -1;
}

int tiku_radio_arch_phy_rx_count(tiku_radio_arch_phy_t phy, uint8_t chan,
                                 uint32_t window_ms, const uint8_t *tag,
                                 uint8_t tag_off, uint8_t tag_len, int8_t *rssi)
{
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t dl =
        (tiku_clock_time_t)(((uint32_t)TIKU_CLOCK_ARCH_SECOND * window_ms) /
                            1000u);
    uint32_t spin = 0u, count = 0u;
    int8_t   last = 0;

    if ((unsigned)phy > 3u) {
        return -1;
    }
    if (chan > 2u) {
        chan = 0u;
    }
    radio_phy_force_disable();
    radio_apply_phy(phy);
    radio_phy_link_aa();
    RADIO->FREQUENCY = adv_freq[chan];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
    RADIO->PACKETPTR = (uint32_t)phy_rxbuf;
    RADIO->SHORTS = (1u << 0);                   /* READY_START only           */
    RADIO->EVENTS_END      = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;
    radio_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;                      /* ramp once, then stay in RX */

    for (;;) {
        if (RADIO->EVENTS_END != 0u) {          /* a packet landed            */
            uint8_t ok = (RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
            RADIO->TASKS_RSSISTART = 1u;
            if (ok && (tag_len == 0u ||
                       memcmp(&phy_rxbuf[tag_off], tag, tag_len) == 0)) {
                count++;
                last = (int8_t)(-(int)(RADIO->RSSISAMPLE & 0x7Fu));
            }
            RADIO->EVENTS_END   = 0u;
            RADIO->EVENTS_CRCOK = 0u;
            RADIO->EVENTS_CRCERROR = 0u;
            RADIO->TASKS_START = 1u;             /* re-RX from RXIDLE (no ramp)*/
        }
        spin++;
        if ((spin & 0xFFFFu) == 0u) {
            radio_hfclk_kick();
            tiku_watchdog_kick();
            if ((tiku_clock_time_t)(tiku_clock_time() - start) >= dl) {
                break;
            }
        }
    }
    RADIO->SHORTS = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    for (spin = 0u; spin < 200000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    if (rssi != 0) {
        *rssi = last;
    }
    return (int)count;
}

/*---------------------------------------------------------------------------*/
/* Connectable advertising + CONNECT_IND capture (L1)                        */
/*---------------------------------------------------------------------------*/
/*
 * First rung of the link-layer ladder (kintsugi/radio.md L-track):
 * transmit ADV_IND (connectable) and open an RX window on the SAME
 * channel immediately after -- a central answers T_IFS=150 us after our
 * packet ends with SCAN_REQ or CONNECT_IND.  The TX->RX turnaround is
 * pure hardware: the DISABLED_RXEN short (bit 3) re-arms the receiver
 * during our post-TX poll's first microseconds, so the radio is
 * listening long before the central's 150 us mark.  The CPU's only
 * timing duty is swapping PACKETPTR to the RX buffer inside the ~40 us
 * ramp -- a couple of register writes after the DISABLED poll hits.
 *
 * L1 deliberately does NOT respond (no SCAN_RSP, no connection): the
 * exit criterion is capturing and decoding a real central's CONNECT_IND
 * LLData off the air.  Fully polled, like every TX-path bring-up here.
 *
 * RAM layout of a captured CONNECT_IND (erratum-49 S1 slot included):
 *   [S0][LEN=34][S1][InitA 6][AdvA 6][LLData 22]
 *    0    1      2   3..8     9..14   15..36
 * LLData: AA(4) CRCInit(3) WinSize(1) WinOffset(2) Interval(2)
 *         Latency(2) Timeout(2) ChM(5) Hop:5|SCA:3 (1).
 */

uint32_t tiku_radio_arch_dbg_connadv_tx;      /* ADV_INDs transmitted     */
uint32_t tiku_radio_arch_dbg_connadv_scanreq; /* SCAN_REQs heard (for us) */
uint32_t tiku_radio_arch_dbg_connadv_rsp;     /* SCAN_RSPs launched (L2)  */
uint32_t tiku_radio_arch_dbg_connadv_tifs;    /* measured RX-end->TX gap  */
uint32_t tiku_radio_arch_dbg_connadv_rxother; /* CRC-OK, not for us       */

/* L2 additions on the same probe: TIFS=150 makes BOTH turnarounds
 * hardware-spaced (the radio times ramp-up so RX opens / TX first-bit
 * lands exactly at T_IFS -- the designed use of the register), and the
 * RX leg arms the DISABLED_TXEN short so a SCAN_RSP launches with zero
 * CPU in the timing path.  The CPU's decision window is the ~70 us
 * before the auto-TX's ramp: SCAN_REQ for us -> swap PACKETPTR to the
 * SCAN_RSP; anything else -> clear the short FIRST, then disable (the
 * order matters -- a disable fires DISABLED, and a still-armed short
 * would chain a garbage TX).  After a launched response, the short is
 * cleared during the TX (post-READY) so its own DISABLED cannot chain.
 *
 * The T_IFS oracle is hardware: TIMER10 free-runs; DPPI ch3 captures
 * CC[3] on every PHYEND (last one before the response = RX end), ch4
 * captures CC[4] on every ADDRESS (last one = our TX's access-address
 * end = T_IFS + 40 us of preamble+AA).  gap = CC4 - CC3 - 40. */
#define CONNADV_DPPI_CH_PHYEND 3u
#define CONNADV_DPPI_CH_ADDR   4u

int tiku_radio_arch_connadv_probe(const uint8_t *addr, const uint8_t *ad,
                                  uint8_t ad_len, uint8_t lldata[22],
                                  uint32_t ms)
{
    static uint8_t adv[48] __attribute__((aligned(4)));
    static uint8_t rsp[48] __attribute__((aligned(4)));
    static uint8_t rx[48] __attribute__((aligned(4)));
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_clock_time_t span =
        (tiku_clock_time_t)((ms * (uint32_t)TIKU_CLOCK_SECOND) / 1000u);
    uint8_t chan = 0u;
    int got = 0;

    /* ADV_IND + SCAN_RSP share the body; only the header type differs. */
    (void)tiku_radio_arch_adv_build(adv, addr, ad, ad_len);
    adv[0] = 0x40u;                            /* ADV_IND, TxAdd=1         */
    (void)tiku_radio_arch_adv_build(rsp, addr, ad, ad_len);
    rsp[0] = 0x44u;                            /* SCAN_RSP, TxAdd=1        */

    tiku_radio_arch_dbg_connadv_tx = 0u;
    tiku_radio_arch_dbg_connadv_scanreq = 0u;
    tiku_radio_arch_dbg_connadv_rsp = 0u;
    tiku_radio_arch_dbg_connadv_tifs = 0u;
    tiku_radio_arch_dbg_connadv_rxother = 0u;

    radio_constlat_enter();                    /* erratum 20 bracket       */
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = 150u;                        /* hardware T_IFS spacing   */

    /* T_IFS measurement fabric: free-running 1 MHz TIMER10 + captures. */
    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->MODE      = 0u;
    NRF_TIMER10_S->BITMODE   = 3u;
    NRF_TIMER10_S->PRESCALER = 4u;
    NRF_TIMER10_S->SHORTS    = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[3] = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[4] = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    RADIO->PUBLISH_PHYEND  = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    RADIO->PUBLISH_ADDRESS = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    NRF_DPPIC10_S->CHENSET = (1u << CONNADV_DPPI_CH_PHYEND) |
                             (1u << CONNADV_DPPI_CH_ADDR);
    NRF_TIMER10_S->TASKS_START = 1u;

    while ((tiku_clock_time_t)(tiku_clock_time() - t0) < span && !got) {
        uint32_t spin;

        tiku_watchdog_kick();

        /* TX leg: auto TX->RX via the DISABLED_RXEN short (RX opens at
         * T_IFS by hardware). */
        RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 3) | (1u << 4);
        RADIO->FREQUENCY = adv_freq[chan];
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
        RADIO->PACKETPTR = (uint32_t)adv;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_ADDRESS  = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        (void)RADIO->EVENTS_DISABLED;
        RADIO->TASKS_TXEN = 1u;
        for (spin = 0u; spin < 400000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        tiku_radio_arch_dbg_connadv_tx++;

        /* RX leg is ramping (hardware short).  Swap to the response
         * shorts -- the RX's own disable now chains a T_IFS-spaced TXEN
         * -- and hand the DMA our buffer, inside the ramp. */
        RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 2) | (1u << 4);
        RADIO->PACKETPTR = (uint32_t)rx;
        RADIO->EVENTS_DISABLED = 0u;
        (void)RADIO->EVENTS_DISABLED;

        /* Listen ~2 ms: a central answers at 150 us; SCAN_REQs from
         * ambient scanners arrive constantly and prove the window. */
        for (spin = 0u; spin < 260000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_DISABLED == 0u) {
            RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
            RADIO->TASKS_DISABLE = 1u;         /* short cleared FIRST      */
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        } else {
            uint8_t type = (uint8_t)(rx[0] & 0x0Fu);
            uint8_t forus = (RADIO->EVENTS_CRCOK != 0u &&
                             memcmp(&rx[9], addr, 6u) == 0) ? 1u : 0u;

            if (forus && type == 0x03u && rx[1] == 12u) {
                /* SCAN_REQ for us: the T_IFS TX is already counting
                 * down in hardware -- give it the SCAN_RSP.  Snapshot
                 * the RX-end capture NOW (the response's own PHYEND
                 * will overwrite CC[3]), wait for the ramp (READY),
                 * then clear the DISABLED_TXEN short DURING the TX so
                 * its end cannot chain another. */
                uint32_t rx_end = NRF_TIMER10_S->CC[3];
                RADIO->PACKETPTR = (uint32_t)rsp;
                RADIO->EVENTS_READY = 0u;
                RADIO->EVENTS_DISABLED = 0u;
                (void)RADIO->EVENTS_DISABLED;
                tiku_radio_arch_dbg_connadv_scanreq++;
                for (spin = 0u; spin < 100000u; spin++) {
                    if (RADIO->EVENTS_READY != 0u) {
                        break;
                    }
                }
                RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
                for (spin = 0u; spin < 400000u; spin++) {
                    if (RADIO->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
                if (RADIO->EVENTS_DISABLED != 0u) {
                    /* CC[4] = our TX's ADDRESS = first bit + 40 us of
                     * preamble+AA; T_IFS = first bit - RX end. */
                    uint32_t gap = NRF_TIMER10_S->CC[4] - rx_end;
                    tiku_radio_arch_dbg_connadv_rsp++;
                    if (gap > 40u && gap < 1000u) {
                        tiku_radio_arch_dbg_connadv_tifs = gap - 40u;
                    }
                }
            } else {
                /* Not our SCAN_REQ: kill the pending auto-TX.  Clear
                 * the short BEFORE disabling -- the disable's DISABLED
                 * event would otherwise chain a garbage TX. */
                RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
                RADIO->TASKS_DISABLE = 1u;
                for (spin = 0u; spin < 40000u; spin++) {
                    if (RADIO->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
                if (forus && type == 0x05u && rx[1] == 34u) {
                    memcpy(lldata, &rx[15], 22u);  /* CONNECT_IND       */
                    got = 1;
                } else if (RADIO->EVENTS_CRCOK != 0u) {
                    tiku_radio_arch_dbg_connadv_rxother++;
                }
            }
        }
        chan = (uint8_t)((chan + 1u) % 3u);
    }

    /* Unwire the measurement fabric + TIFS. */
    RADIO->PUBLISH_PHYEND  = 0u;
    RADIO->PUBLISH_ADDRESS = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[3] = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[4] = 0u;
    NRF_DPPIC10_S->CHENCLR = (1u << CONNADV_DPPI_CH_PHYEND) |
                             (1u << CONNADV_DPPI_CH_ADDR);
    NRF_TIMER10_S->TASKS_STOP = 1u;
    RADIO->TIFS = 0u;

    /* Restore the TX-only shorts contract. */
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    {
        uint32_t spin;
        for (spin = 0u; spin < 40000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
    }
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    radio_constlat_exit();
    return got;
}

/*---------------------------------------------------------------------------*/
/* CSA#1 channel selection (L3 groundwork)                                   */
/*---------------------------------------------------------------------------*/
/*
 * Channel Selection Algorithm #1 (Core Vol 6 Part B 4.5.8.2), the hop
 * engine every legacy connection uses:
 *   unmapped = (lastUnmapped + hopIncrement) mod 37
 *   used?    -> channel = unmapped
 *   unused?  -> channel = usedChannels[unmapped mod numUsed] (ascending)
 * lastUnmapped advances to `unmapped` either way -- the classic
 * implementation bug is advancing to the REMAPPED channel.
 * Verified against an independent Python implementation via the baked
 * vectors in `bleadv csa1` (three maps incl. heavy remapping).
 */

uint8_t tiku_radio_ll_csa1_next(uint8_t last_unmapped, uint8_t hop,
                                const uint8_t chmap[5],
                                uint8_t *unmapped_out)
{
    uint8_t un = (uint8_t)((last_unmapped + hop) % 37u);
    uint8_t n = 0u, idx, c;

    *unmapped_out = un;
    if (chmap[un >> 3] & (uint8_t)(1u << (un & 7u))) {
        return un;
    }
    for (c = 0u; c < 37u; c++) {               /* numUsed                  */
        if (chmap[c >> 3] & (uint8_t)(1u << (c & 7u))) {
            n++;
        }
    }
    idx = (uint8_t)(un % n);
    for (c = 0u; c < 37u; c++) {               /* idx-th used, ascending   */
        if (chmap[c >> 3] & (uint8_t)(1u << (c & 7u))) {
            if (idx == 0u) {
                return c;
            }
            idx--;
        }
    }
    return 0u;                                 /* unreachable (n >= 1)     */
}

/*---------------------------------------------------------------------------*/
/* Data-PDU acknowledgement / flow control (L3 groundwork)                   */
/*---------------------------------------------------------------------------*/
/*
 * The SN/NESN 1-bit sliding window (Core Vol 6 Part B 4.5.9), the other
 * classic link-layer bug-nest.  Each side carries in every Data PDU
 * header its own SN (the seq number of the PDU it is sending) and NESN
 * (the seq number it expects next = an ack of the peer's last PDU).
 *
 * On receiving a CRC-valid Data PDU (rx_sn, rx_nesn, has_payload):
 *   - ACK of MY transmission: rx_nesn != my sn  =>  peer moved past my
 *     SN, so my PDU landed -> flip sn, load the next TX PDU.  Equal =>
 *     NAK, retransmit the SAME PDU.
 *   - NEW data from peer: rx_sn == my nesn AND the PDU carries payload
 *     =>  not a retransmission -> deliver, flip nesn.  Unequal/empty =>
 *     a resend or a keepalive I already have -> discard (ACK half still
 *     applies).
 * Both flips are INDEPENDENT: a single PDU can ack my TX and carry new
 * data.  CRITICAL: NESN advances only for PAYLOAD-bearing PDUs, never for
 * empty keepalives.  Otherwise a stream of empties walks my NESN past the
 * peer's SN, and the peer then reads my (advanced) NESN as an ACK of data
 * it never received -- a false ACK that silently drops the payload.
 * Gating NESN on payload makes it a trustworthy "I got your data" signal,
 * so the sender can retransmit until GENUINELY delivered (exactly-once),
 * which unsolicited notifications rely on (there is no app-level reply to
 * confirm them).  Empties stay flow-control-neutral except for the ACK
 * half, which they still legitimately carry.
 */

uint8_t tiku_radio_ll_ack(tiku_radio_ll_ack_t *a, uint8_t rx_sn,
                          uint8_t rx_nesn, uint8_t has_payload)
{
    uint8_t r = 0u;

    if ((rx_nesn & 1u) != a->sn) {             /* my last PDU acked        */
        a->sn ^= 1u;
        r |= TIKU_RADIO_LL_ACKED;
    }
    if (has_payload && (rx_sn & 1u) == a->nesn) {  /* genuinely new data   */
        a->nesn ^= 1u;
        r |= TIKU_RADIO_LL_NEWDATA;
    }
    return r;
}

/*---------------------------------------------------------------------------*/
/* Connection engine, PERIPHERAL role (L3)                                   */
/*---------------------------------------------------------------------------*/
/*
 * The spine: hold a live BLE connection with a real central.  Everything
 * hard is already proven -- L2's hardware T_IFS turnaround (RX -> 150 us
 * -> auto TX, the exact short chain used here), CSA#1 hopping, and the
 * SN/NESN ack window.  L3 threads them through a per-connection-event
 * loop clocked by a free-running TIMER10 (1 MHz absolute timebase):
 *
 *   advertise ADV_IND -> capture CONNECT_IND (reuses the L1 path) and its
 *   end time -> first anchor = end + transmitWindowDelay(1250us) +
 *   WinOffset*1250 -> per event { wait the anchor, RX on the CSA#1 data
 *   channel, hardware-T_IFS respond with an empty PDU carrying our
 *   SN/NESN, re-sync the anchor to the packet's actual arrival } until
 *   the supervision timeout (no CRC-valid packet) or the caller's cap.
 *
 * Blocking + polled, like every bring-up path here (the shell is parked
 * while connected; L6 makes it a background service).  Drift is handled
 * by re-syncing to each packet's arrival, so a small per-event window
 * suffices after the first.  Empty PDUs only -- no LL control (L4), no
 * data (L5), no encryption; enough to keep the link UP, which is L3's
 * whole exit criterion.
 */

#define BLE_TX_WIN_DELAY_US  1250u    /* LE 1M transmitWindowDelay        */
#define CONN_PREROLL_US      300u     /* open RX this far before the anchor */

/* Free-running 1 MHz timebase for the whole connection (CC[2]=now reads,
 * CC[1]=per-packet anchor capture). */
static void conn_timer_start(void)
{
    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->MODE      = 0u;
    NRF_TIMER10_S->BITMODE   = 3u;            /* 32-bit                    */
    NRF_TIMER10_S->PRESCALER = 4u;            /* 16 MHz/16 = 1 MHz         */
    NRF_TIMER10_S->SHORTS    = 0u;
    NRF_TIMER10_S->TASKS_START = 1u;
}

static uint32_t conn_now(void)
{
    NRF_TIMER10_S->TASKS_CAPTURE[2] = 1u;
    return NRF_TIMER10_S->CC[2];
}

/* Data channel index k (0..36) -> RADIO FREQUENCY offset (MHz - 2400),
 * skipping the three advertising channels. */
static uint8_t ble_data_chan_freq(uint8_t k)
{
    return (k <= 10u) ? (uint8_t)(4u + 2u * k)
                      : (uint8_t)(28u + 2u * (k - 11u));
}

static void radio_cfg_data(uint32_t aa, uint32_t crcinit, uint8_t k)
{
    RADIO->BASE0     = aa << 8;                /* BALEN=3: base in top 3 B  */
    RADIO->PREFIX0   = (aa >> 24) & 0xFFu;
    RADIO->CRCINIT   = crcinit & 0x00FFFFFFul;
    RADIO->FREQUENCY = ble_data_chan_freq(k);
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | k);   /* data-channel IV   */
}

/*---------------------------------------------------------------------------*/
/* LL control PDUs (L4) -- shared by both roles (own state per board)        */
/*---------------------------------------------------------------------------*/
/*
 * A minimal, conformant LL control layer on top of the empty-PDU link:
 * one pending control PDU at a time (retransmitted via SN/NESN until the
 * peer acks), request/response handling for VERSION/FEATURE/PING, and
 * LL_UNKNOWN_RSP for anything unrecognised (so a peer probing us never
 * hangs).  PHY-independent; the connection engine calls ll_build_tx()
 * for what to send and ll_handle_rx() for what arrived.
 */
#define LL_CONNECTION_UPDATE 0x00u
#define LL_CHANNEL_MAP_IND   0x01u
#define LL_TERMINATE_IND     0x02u
#define LL_UNKNOWN_RSP       0x07u
#define LL_FEATURE_REQ       0x08u
#define LL_FEATURE_RSP       0x09u
#define LL_VERSION_IND       0x0Cu
#define LL_PING_REQ          0x12u
#define LL_PING_RSP          0x13u

static uint8_t  ll_tx[36];          /* pending PDU RAM [hdr][len][S1][pay] */
static uint8_t  ll_tx_len;          /* payload len; 0 = none               */
static uint8_t  ll_tx_llid;         /* 2 = L2CAP data, 3 = LL control      */
static uint8_t  ll_peer_vers;       /* peer VersNr (0 = not yet heard)     */
static uint8_t  ll_sent_vers;       /* we have queued our VERSION_IND      */
static uint8_t  ll_want_term;       /* peer sent TERMINATE_IND             */
static uint8_t  ll_is_central;      /* role: drives ATT client vs server   */
static uint32_t ll_ctrl_tx, ll_ctrl_rx;

/* ATT/L2CAP (L5): a minimal NUS (Nordic UART Service) data path.
 *   peripheral = server: NUS RX (write target) + NUS TX (readable echo)
 *                behind a CCCD; a write to NUS RX is mirrored to TX.
 *   central    = client: MTU -> enable CCCD -> write "TK" to NUS RX ->
 *                read TX back and verify the echo.
 * The loopback is verified over the RELIABLE request/response path (each
 * request is retransmitted until answered): server-PUSHED notifications
 * on this on-die link are dropped by the peer's SN/NESN dedup and need a
 * proper LL reliability rework -- deferred (see kintsugi/radio.md L5).
 * This is the byte-pipe L6's tiku_ble_serial rides on.  Fixed handles
 * (a phone would discover them; the two-board client hard-codes them). */
#define GATT_H_NUS_RX    0x0012u    /* write:  client -> server            */
#define GATT_H_NUS_TX    0x0014u    /* readable echo (notify src, future)  */
#define GATT_H_NUS_CCCD  0x0015u    /* CCCD for the TX characteristic       */
static const uint8_t NUS_TEST_MSG[2] = { 'T', 'K' };

static uint8_t  nus_rx[20];         /* server: last bytes written to RX     */
static uint8_t  nus_rx_len;
static uint8_t  nus_cccd;           /* server: TX notifications enabled bit */
static uint8_t  nus_notify[20];     /* server: pending notification payload */
static uint8_t  nus_notify_len;     /* server: >0 = queue a TX notification */
/* Client steps: 1 mtu, 2 disc-service, 3 disc-chars, 4 disc-cccd,
 * 5 cccd-write, 6 rx-write, 7 await-notify, 8 done.  Discovery walks the
 * peer's GATT DB (Read By Group Type / Read By Type / Find Info) and uses
 * the DISCOVERED handles for the NUS ops (falling back to the well-known
 * ones if a step returns nothing), so it exercises the same ATT a phone
 * would -- kintsugi/radio.md L6 GATT discovery. */
static uint8_t  att_step;
static uint8_t  att_ok;             /* client: notification echo matched    */
static uint8_t  att_readback;       /* client/server: first data byte seen  */
static uint8_t  att_disc_ok;        /* client: discovered handles as expected */
static uint16_t att_d_rx, att_d_tx, att_d_cccd; /* discovered NUS handles   */
static uint16_t att_d_next;         /* Read-By-Type iteration cursor         */

static void ll_reset(void)
{
    ll_tx_len = 0u; ll_tx_llid = 0u; ll_peer_vers = 0u; ll_sent_vers = 0u;
    ll_want_term = 0u; ll_ctrl_tx = 0u; ll_ctrl_rx = 0u;
    nus_rx_len = 0u; nus_cccd = 0u; nus_notify_len = 0u;
    att_step = 0u; att_ok = 0u; att_readback = 0u;
    att_disc_ok = 0u; att_d_rx = 0u; att_d_tx = 0u; att_d_cccd = 0u;
    att_d_next = 0u;
}

/* Queue a raw LL payload with the given LLID (2 L2CAP / 3 control). */
static void ll_queue_raw(uint8_t llid, const uint8_t *payload, uint8_t plen)
{
    if (ll_tx_len != 0u || plen == 0u) {
        return;                     /* one PDU in flight at a time         */
    }
    ll_tx_llid = llid;
    ll_tx[1] = plen;
    ll_tx[2] = payload[0];          /* S1 = payload[0] (erratum-49)        */
    memcpy(&ll_tx[3], payload, plen);
    ll_tx_len = plen;
}

static void ll_queue(uint8_t opcode, const uint8_t *data, uint8_t dlen)
{
    uint8_t p[34];
    p[0] = opcode;
    if (dlen) {
        memcpy(&p[1], data, dlen);
    }
    ll_queue_raw(3u, p, (uint8_t)(1u + dlen));
}

/* Wrap an ATT PDU in an L2CAP frame (CID 0x0004) and queue as LLID=2. */
static void att_queue(const uint8_t *att, uint8_t alen)
{
    uint8_t p[34];
    p[0] = alen; p[1] = 0u;         /* L2CAP length                        */
    p[2] = 0x04u; p[3] = 0x00u;     /* CID = ATT                           */
    memcpy(&p[4], att, alen);
    ll_queue_raw(2u, p, (uint8_t)(4u + alen));
}

static void ll_queue_version(void)
{
    /* VersNr 0x0C (5.3), CompId 0x0059 (Nordic), SubVersNr 0x0001. */
    static const uint8_t v[5] = { 0x0Cu, 0x59u, 0x00u, 0x01u, 0x00u };
    ll_queue(LL_VERSION_IND, v, 5u);
    ll_sent_vers = 1u;
}

/* Build the outgoing PDU (pending control, else empty) with SN/NESN into
 * @p out; returns total RAM bytes ([hdr][len][S1]+payload). */
static uint8_t ll_build_tx(uint8_t *out, const tiku_radio_ll_ack_t *ack)
{
    if (ll_tx_len != 0u) {
        memcpy(out, ll_tx, (size_t)(3u + ll_tx_len));
        out[0] = (uint8_t)((ll_tx_llid & 0x03u) |
                           (ack->nesn << 2) | (ack->sn << 3));
        out[2] = out[3];            /* S1 = payload[0]                     */
        return (uint8_t)(3u + ll_tx_len);
    }
    out[0] = (uint8_t)(0x01u | (ack->nesn << 2) | (ack->sn << 3));
    out[1] = 0u;
    out[2] = out[0];
    return 3u;
}

/* Our last TX was acknowledged (SN advanced): a pending control PDU got
 * through -- count it and clear the slot. */
static void ll_on_acked(void)
{
    if (ll_tx_len != 0u) {
        ll_ctrl_tx++;
        ll_tx_len = 0u;
    }
}

/* ATT client (L5): queue the request for the current step.  Used for the
 * initial send, each step advance, AND retransmission -- the link drops a
 * single-shot data PDU when the SN/NESN offset momentarily disfavours it
 * (the peer acks-but-ignores a "duplicate" sn), so the client re-sends
 * until the response lands.  Every request here is idempotent. */
static void att_client_send(void)
{
    uint8_t  b[7];
    uint16_t h_rx   = att_d_rx   ? att_d_rx   : GATT_H_NUS_RX;
    uint16_t h_cccd = att_d_cccd ? att_d_cccd : GATT_H_NUS_CCCD;

    if (att_step == 1u) {                       /* Exchange MTU Request    */
        b[0] = 0x02u; b[1] = 23u; b[2] = 0u;
        att_queue(b, 3u);
    } else if (att_step == 2u) {                /* Read By Group Type      */
        b[0] = 0x10u;                           /* discover primary svcs   */
        b[1] = 0x01u; b[2] = 0x00u;             /* start = 0x0001          */
        b[3] = 0xFFu; b[4] = 0xFFu;             /* end   = 0xFFFF          */
        b[5] = 0x00u; b[6] = 0x28u;             /* type = 0x2800           */
        att_queue(b, 7u);
    } else if (att_step == 3u) {                /* Read By Type (chars)    */
        uint16_t s = att_d_next ? att_d_next : 0x0001u;
        b[0] = 0x08u;
        b[1] = (uint8_t)s; b[2] = (uint8_t)(s >> 8);
        b[3] = 0xFFu; b[4] = 0xFFu;
        b[5] = 0x03u; b[6] = 0x28u;             /* type = 0x2803           */
        att_queue(b, 7u);
    } else if (att_step == 4u) {                /* Find Information (CCCD) */
        uint16_t s = att_d_tx ? (uint16_t)(att_d_tx + 1u) : 0x0001u;
        b[0] = 0x04u;
        b[1] = (uint8_t)s; b[2] = (uint8_t)(s >> 8);
        b[3] = 0xFFu; b[4] = 0xFFu;
        att_queue(b, 5u);
    } else if (att_step == 5u) {                /* enable TX notifications  */
        b[0] = 0x12u;                           /* Write CCCD = 0x0001     */
        b[1] = (uint8_t)h_cccd; b[2] = (uint8_t)(h_cccd >> 8);
        b[3] = 0x01u; b[4] = 0x00u;
        att_queue(b, 5u);
    } else if (att_step == 6u) {                /* write NUS RX ("TK")     */
        b[0] = 0x12u;
        b[1] = (uint8_t)h_rx; b[2] = (uint8_t)(h_rx >> 8);
        b[3] = NUS_TEST_MSG[0]; b[4] = NUS_TEST_MSG[1];
        att_queue(b, 5u);
    }
    /* step 7 awaits the server's pushed TX notification -- nothing to send */
}

/* ATT dispatch (L5): buf is the ATT PDU, len its length. */
static void att_handle(const uint8_t *att, uint8_t alen)
{
    uint8_t op = att[0];

    if (ll_is_central) {
        /* CLIENT: MTU -> GATT discovery -> CCCD -> write RX -> await notify.
         * On an ATT Error (0x01) a discovery phase is simply "done", so we
         * advance; missing handles fall back to the well-known ones. */
        uint16_t h_tx = att_d_tx ? att_d_tx : GATT_H_NUS_TX;

        if (op == 0x03u && att_step == 1u) {        /* MTU Response        */
            att_step = 2u;
            att_client_send();                      /* Read By Group Type  */
        } else if (op == 0x11u && att_step == 2u) { /* svc discovered      */
            att_d_next = (uint16_t)(att[2] | ((uint16_t)att[3] << 8));
            att_step = 3u;
            att_client_send();                      /* Read By Type        */
        } else if (op == 0x09u && att_step == 3u && alen >= 21u) {
            /* char decl value = [props][vhandle(2)][UUID(16)]; NUS variant
             * is UUID byte 12 -> att[7+12] = att[19]. */
            uint16_t vh = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
            if (att[19] == 0x02u) {
                att_d_rx = vh;
            } else if (att[19] == 0x03u) {
                att_d_tx = vh;
            }
            att_d_next = (uint16_t)((att[2] | ((uint16_t)att[3] << 8)) + 1u);
            if (att_d_rx != 0u && att_d_tx != 0u) {
                att_step = 4u;                      /* both chars found    */
            }
            att_client_send();                      /* next RdByType / FindInfo */
        } else if (op == 0x05u && att_step == 4u && alen >= 6u) {
            if (att[4] == 0x02u && att[5] == 0x29u) {   /* CCCD 0x2902      */
                att_d_cccd = (uint16_t)(att[2] | ((uint16_t)att[3] << 8));
            }
            att_disc_ok = (uint8_t)(att_d_rx == GATT_H_NUS_RX &&
                                    att_d_tx == GATT_H_NUS_TX &&
                                    att_d_cccd == GATT_H_NUS_CCCD);
            att_step = 5u;
            att_client_send();                      /* enable CCCD         */
        } else if (op == 0x01u && att_step >= 2u && att_step <= 4u) {
            /* ATT Error ends a discovery phase; advance best-effort. */
            if (att_step == 3u) {
                att_step = 4u;
            } else {
                att_disc_ok = (uint8_t)(att_d_rx == GATT_H_NUS_RX &&
                                        att_d_tx == GATT_H_NUS_TX &&
                                        att_d_cccd == GATT_H_NUS_CCCD);
                att_step = 5u;
            }
            att_client_send();
        } else if (op == 0x13u && att_step == 5u) { /* CCCD Write Response */
            att_step = 6u;
            att_client_send();                      /* write NUS RX        */
        } else if (op == 0x13u && att_step == 6u) { /* RX Write Response   */
            att_step = 7u;                          /* now await notify    */
        } else if (op == 0x1Bu && att_step == 7u) { /* Handle Value Notify */
            if (alen >= 5u && att[1] == (uint8_t)h_tx &&
                att[2] == (uint8_t)(h_tx >> 8)) {
                att_readback = att[3];              /* echoed first byte   */
                att_ok = (uint8_t)(att[3] == NUS_TEST_MSG[0] &&
                                   att[4] == NUS_TEST_MSG[1]);
                att_step = 8u;                      /* loopback verified   */
            }
        }
    } else {
        /* SERVER: MTU; CCCD + RX writes; a RX write pushes a TX notify. */
        if (op == 0x02u) {                          /* Exchange MTU Req    */
            uint8_t m[3] = { 0x03u, 23u, 0u };      /* MTU Rsp, 23         */
            att_queue(m, 3u);
        } else if (op == 0x12u && alen >= 4u) {     /* Write Request       */
            uint16_t h = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
            if (h == GATT_H_NUS_CCCD) {
                nus_cccd = att[3];                  /* TX notify enable bit */
            } else if (h == GATT_H_NUS_RX) {
                uint8_t n = (uint8_t)(alen - 3u);   /* value byte count    */
                if (n > sizeof(nus_rx)) {
                    n = (uint8_t)sizeof(nus_rx);
                }
                memcpy(nus_rx, &att[3], n);
                nus_rx_len = n;
                att_readback = att[3];              /* stats: first byte   */
                if (nus_cccd & 0x01u) {             /* loop back to TX      */
                    memcpy(nus_notify, &att[3], n);
                    nus_notify_len = n;
                }
            }
            {
                uint8_t rsp = 0x13u;                /* Write Response      */
                att_queue(&rsp, 1u);
            }
        }
    }
}

/* Server (L5): if a NUS TX notification is pending and the slot is free,
 * queue it as a Handle Value Notification.  Reliable now that NESN only
 * advances on payload -- the LL retransmits it until genuinely acked.
 * Called from the peripheral connection loop between events. */
static void att_server_pump(void)
{
    if (nus_notify_len != 0u && ll_tx_len == 0u) {
        uint8_t nb[3 + sizeof(nus_notify)];
        nb[0] = 0x1Bu;                              /* Handle Value Notify */
        nb[1] = (uint8_t)GATT_H_NUS_TX;
        nb[2] = (uint8_t)(GATT_H_NUS_TX >> 8);
        memcpy(&nb[3], nus_notify, nus_notify_len);
        att_queue(nb, (uint8_t)(3u + nus_notify_len));
        nus_notify_len = 0u;
    }
}

/* Process a genuinely-new received PDU (RAM [hdr][len][S1][payload], so
 * payload starts at buf[3]); may queue a response. */
static void ll_handle_rx(const uint8_t *buf)
{
    uint8_t llid = buf[0] & 0x03u;
    uint8_t op;

    if (buf[1] == 0u) {
        return;                     /* empty PDU                           */
    }
    if (llid == 0x02u) {
        /* L2CAP data: [len(2)][CID(2)][payload].  ATT is CID 0x0004. */
        if (buf[1] >= 5u && buf[5] == 0x04u && buf[6] == 0x00u) {
            ll_ctrl_rx++;
            att_handle(&buf[7], (uint8_t)(buf[1] - 4u));
        }
        return;
    }
    if (llid != 0x03u) {
        return;                     /* not control                         */
    }
    op = buf[3];
    ll_ctrl_rx++;
    switch (op) {
    case LL_VERSION_IND:
        if (buf[1] >= 2u) {
            ll_peer_vers = buf[4];
        }
        if (!ll_sent_vers) {
            ll_queue_version();     /* reply with ours                     */
        } else if (ll_is_central && att_step == 0u) {
            att_step = 1u;              /* L5: kick off ATT (MTU Request)  */
            att_client_send();
        }
        break;
    case LL_FEATURE_REQ: {
        static const uint8_t none[8] = { 0u };
        ll_queue(LL_FEATURE_RSP, none, 8u);
        break;
    }
    case LL_PING_REQ:
        ll_queue(LL_PING_RSP, (const uint8_t *)0, 0u);
        break;
    case LL_TERMINATE_IND:
        ll_want_term = 1u;
        break;
    case LL_FEATURE_RSP:
    case LL_PING_RSP:
    case LL_UNKNOWN_RSP:
        break;                      /* responses: just counted             */
    default:
        ll_queue(LL_UNKNOWN_RSP, &op, 1u);   /* decline gracefully         */
        break;
    }
}

/*
 * One connection event: RX the central's PDU on data channel @p k, then
 * hardware-T_IFS respond with an empty PDU carrying our SN/NESN.  The
 * short chain (READY_START | PHYEND_DISABLE | DISABLED_TXEN) is exactly
 * L2's proven SCAN_RSP turnaround.  @p deadline bounds the RX wait
 * (absolute TIMER10 us).  Returns 2 = CRC-valid packet, 1 = packet seen
 * but CRC bad, 0 = window elapsed with nothing.  On >=1, *anchor gets
 * the packet's ADDRESS time.
 */
/* First CRC-failed packet's RAM bytes, for the whitening/CRC diagnostic. */
static uint8_t conn_fail_snap[5];
static uint8_t conn_fail_have;

static int conn_event(uint32_t aa, uint32_t crcinit, uint8_t k,
                      tiku_radio_ll_ack_t *ack, uint32_t deadline,
                      uint32_t *anchor)
{
    static uint8_t rxb[48] __attribute__((aligned(4)));
    static uint8_t txb[36] __attribute__((aligned(4)));
    uint32_t spin;
    uint8_t  crcok, txn;

    int      got = 0;

    radio_cfg_data(aa, crcinit, k);

    RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 2) | (1u << 4);
    RADIO->PACKETPTR = (uint32_t)rxb;
    RADIO->EVENTS_ADDRESS  = 0u;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_RXEN = 1u;

    while ((int32_t)(conn_now() - deadline) < 0) {
        if (RADIO->EVENTS_ADDRESS != 0u) {
            got = 1;
            break;
        }
    }
    if (!got) {
        RADIO->SHORTS = (1u << 0) | (1u << 19);   /* drop the auto-TX arm  */
        RADIO->TASKS_DISABLE = 1u;
        for (spin = 0u; spin < 40000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        return 0;
    }
    NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;         /* anchor = ADDRESS time */
    *anchor = NRF_TIMER10_S->CC[1];

    /* Wait for the CRC VERDICT, not just PHYEND: CRCOK/CRCERROR fire a
     * moment AFTER the last bit (the CRC is validated post-PHYEND), so
     * sampling EVENTS_CRCOK right at PHYEND catches it only for the
     * shortest packets and misses it for payload-bearing ones (measured:
     * empty PDU rx_ok, VERSION_IND "addr-only" despite valid bytes).
     * The hardware T_IFS TX is already ramping regardless -- we still
     * have ~150 us to stage the response before its DMA starts. */
    for (spin = 0u; spin < 400000u; spin++) {
        if (RADIO->EVENTS_CRCOK != 0u || RADIO->EVENTS_CRCERROR != 0u) {
            break;
        }
    }
    crcok = (RADIO->EVENTS_CRCOK != 0u) ? 1u : 0u;
    if (!crcok && !conn_fail_have) {
        memcpy(conn_fail_snap, rxb, 5u);          /* snapshot for diag     */
        conn_fail_have = 1u;
    }
    if (crcok) {
        /* Advance SN/NESN only on a CRC-valid packet; on a bad CRC our
         * NESN stays put -> the response is an implicit NAK -> the
         * central retransmits. */
        uint8_t h = rxb[0];
        uint8_t r = tiku_radio_ll_ack(ack, (uint8_t)((h >> 3) & 1u),
                                      (uint8_t)((h >> 2) & 1u),
                                      (uint8_t)(rxb[1] != 0u));
        if (r & TIKU_RADIO_LL_ACKED) {
            ll_on_acked();                        /* our last PDU landed   */
        }
        if (r & TIKU_RADIO_LL_NEWDATA) {
            ll_handle_rx(rxb);                    /* L4: control PDUs      */
        }
    }
    /* Build our response (pending LL control PDU, else empty) with the
     * updated SN/NESN.  The hardware T_IFS TX DMAs it. */
    txn = ll_build_tx(txb, ack);
    (void)txn;
    /* Drop DISABLED_TXEN now that the RX->TX turnaround has fired: keep
     * only READY_START + PHYEND_DISABLE for the response.  Otherwise the
     * response's own DISABLED re-triggers TXEN and the radio spins a
     * spurious back-to-back TX loop -- benign for a short empty PDU (it
     * clears before the next RX), but a longer PDU (an ATT notification)
     * is still churning when the next event's RXEN fires and the RX is
     * lost, collapsing the link. */
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    RADIO->EVENTS_PHYEND   = 0u;                  /* next PHYEND = TX end  */
    RADIO->EVENTS_DISABLED = 0u;                  /* next DISABLED = TX    */
    RADIO->PACKETPTR = (uint32_t)txb;

    for (spin = 0u; spin < 400000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    return crcok ? 2 : 1;
}

int tiku_radio_arch_connect(const uint8_t *addr, const uint8_t *ad,
                            uint8_t ad_len, uint32_t max_secs,
                            tiku_radio_ll_conn_stats_t *st)
{
    uint8_t  lldata[22];
    uint32_t aa, crcinit, interval_us, timeout_us;
    uint32_t anchor, last_valid, t_ci_end, cap_deadline;
    uint16_t winoff;
    uint8_t  winsize, hop, last_unmapped = 0u, first = 1u;
    tiku_radio_ll_ack_t ack = { 0u, 0u };

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        st->events = 0u;
        st->rx_ok = 0u;
        st->addr_seen = 0u;
        st->missed = 0u;
        st->ms = 0u;
        st->first_chan = 0u;
        st->hop = 0u;
        st->reason = 2u;                          /* never connected       */
    }

    tiku_radio_arch_init();
    radio_constlat_enter();                       /* erratum 20            */
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = 150u;
    conn_fail_have = 0u;
    conn_timer_start();

    /* --- Advertising phase: ADV_IND until a CONNECT_IND for us --- */
    {
        static uint8_t adv[48] __attribute__((aligned(4)));
        static uint8_t rx[48]  __attribute__((aligned(4)));
        uint8_t chan = 0u;
        int connected = 0;

        (void)tiku_radio_arch_adv_build(adv, addr, ad, ad_len);
        adv[0] = 0x40u;                           /* ADV_IND, TxAdd=1      */
        cap_deadline = conn_now() + max_secs * 1000000u;

        while (!connected &&
               (int32_t)(conn_now() - cap_deadline) < 0) {
            uint32_t spin;
            tiku_watchdog_kick();

            /* TX ADV_IND, hardware turnaround to RX (DISABLED_RXEN). */
            RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 3) | (1u << 4);
            RADIO->BASE0   = BLE_ADV_ACCESS_BASE0;
            RADIO->PREFIX0 = BLE_ADV_ACCESS_PREFIX0;
            RADIO->CRCINIT = BLE_ADV_CRC_INIT;
            RADIO->FREQUENCY = adv_freq[chan];
            RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
            RADIO->PACKETPTR = (uint32_t)adv;
            RADIO->EVENTS_DISABLED = 0u;
            (void)RADIO->EVENTS_DISABLED;
            RADIO->TASKS_TXEN = 1u;
            for (spin = 0u; spin < 400000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            /* RX leg is ramping; hand it our buffer, drop the turnaround
             * short so its own disable can't chain a TX. */
            RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
            RADIO->PACKETPTR = (uint32_t)rx;
            RADIO->EVENTS_DISABLED = 0u;
            RADIO->EVENTS_CRCOK    = 0u;
            (void)RADIO->EVENTS_DISABLED;
            for (spin = 0u; spin < 260000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            if (RADIO->EVENTS_DISABLED == 0u) {
                RADIO->TASKS_DISABLE = 1u;
                for (spin = 0u; spin < 40000u; spin++) {
                    if (RADIO->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
            } else if (RADIO->EVENTS_CRCOK != 0u &&
                       (rx[0] & 0x0Fu) == 0x05u && rx[1] == 34u &&
                       memcmp(&rx[9], addr, 6u) == 0) {
                NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
                t_ci_end = NRF_TIMER10_S->CC[1];  /* ~CONNECT_IND end      */
                memcpy(lldata, &rx[15], 22u);
                connected = 1;
            }
            chan = (uint8_t)((chan + 1u) % 3u);
        }
        if (!connected) {
            RADIO->TIFS = 0u;
            NRF_TIMER10_S->TASKS_STOP = 1u;
            RADIO->SHORTS = (1u << 0) | (1u << 19);
            radio_constlat_exit();
            return -1;
        }
    }

    /* --- Parse the LLData into connection parameters --- */
    aa = (uint32_t)lldata[0] | ((uint32_t)lldata[1] << 8) |
         ((uint32_t)lldata[2] << 16) | ((uint32_t)lldata[3] << 24);
    crcinit = (uint32_t)lldata[4] | ((uint32_t)lldata[5] << 8) |
              ((uint32_t)lldata[6] << 16);
    winsize = lldata[7];
    winoff  = (uint16_t)(lldata[8] | ((uint16_t)lldata[9] << 8));
    interval_us = (uint32_t)(lldata[10] | ((uint32_t)lldata[11] << 8))
                  * 1250u;
    timeout_us  = (uint32_t)(lldata[14] | ((uint32_t)lldata[15] << 8))
                  * 10000u;
    hop = (uint8_t)(lldata[21] & 0x1Fu);
    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        uint8_t un0;
        st->hop = hop;
        st->first_chan = tiku_radio_ll_csa1_next(0u, hop, lldata + 16, &un0);
        st->interval = (uint16_t)(lldata[10] | ((uint16_t)lldata[11] << 8));
        st->winoff = winoff;
        st->winsize = winsize;
    }

    /* --- Connection loop --- */
    ll_reset();                                   /* L4/L5: LL state       */
    ll_is_central = 0u;                           /* peripheral = ATT server */
    anchor = t_ci_end + BLE_TX_WIN_DELAY_US + (uint32_t)winoff * 1250u;
    last_valid = t_ci_end;
    if (interval_us == 0u) {
        interval_us = 1250u;                      /* defensive             */
    }

    for (;;) {
        uint8_t  k;
        uint32_t rxen_at, deadline, t_addr = anchor;
        int      r;

        k = tiku_radio_ll_csa1_next(last_unmapped, hop, lldata + 16,
                                    &last_unmapped);
        /* Pre-roll: open RX ~300 us BEFORE the anchor so the receiver has
         * finished ramping (~40 us) and is listening when the central's
         * single per-event packet arrives.  Without this the preamble
         * lands during RX ramp-up and every event is missed (measured:
         * addr_seen=0).  The first event's tail is widened by the
         * transmitWindowSize uncertainty. */
        /* First window spans the transmitWindowSize (the master transmits
         * at the window START); narrow after re-sync.  A huge first
         * window catches garbage far from the anchor and re-syncs wrong. */
        rxen_at = anchor - 600u;
        deadline = first ? (anchor + (uint32_t)winsize * 1250u + 1000u)
                         : (anchor + 2500u);

        while ((int32_t)(conn_now() - rxen_at) < 0) {
            /* park to the anchor; kick occasionally */
        }
        r = conn_event(aa, crcinit, k, &ack, deadline, &t_addr);
        att_server_pump();                    /* L5: push pending NUS notify */
        if (st != (tiku_radio_ll_conn_stats_t *)0) {
            st->events++;
        }
        if (r >= 1) {
            if (first && st != (tiku_radio_ll_conn_stats_t *)0) {
                st->first_delta = (int32_t)(t_addr - anchor);
            }
            anchor = t_addr + interval_us;        /* re-sync to arrival    */
            first = 0u;
        } else {
            anchor = anchor + interval_us;        /* nominal advance       */
        }
        if (st != (tiku_radio_ll_conn_stats_t *)0) {
            if (r == 2) {
                st->rx_ok++;
            } else if (r == 1) {
                st->addr_seen++;      /* AA matched, CRC bad: decode diag  */
            } else {
                st->missed++;
            }
        }
        if (r == 2) {
            last_valid = t_addr;
        }

        tiku_watchdog_kick();

        if (ll_want_term) {                       /* L4: peer terminated   */
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 3u;
            }
            break;
        }
        if ((int32_t)(conn_now() - (last_valid + timeout_us)) >= 0) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 1u;                  /* supervision timeout   */
            }
            break;
        }
        if ((int32_t)(conn_now() - cap_deadline) >= 0) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 0u;                  /* caller's cap           */
            }
            break;
        }
    }

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        st->ms = (conn_now() - t_ci_end) / 1000u;
        st->peer_vers = ll_peer_vers;
        st->ctrl_tx = ll_ctrl_tx;
        st->ctrl_rx = ll_ctrl_rx;
        st->att_step = att_step;
        st->att_ok = att_ok;
        st->att_readback = att_readback;
        memcpy(st->fail_bytes, conn_fail_snap, 5u);
    }

    /* Teardown: radio idle, TX shorts + advertising CRC restored. */
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    {
        uint32_t spin;
        for (spin = 0u; spin < 40000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
    }
    RADIO->TIFS = 0u;
    RADIO->CRCINIT = BLE_ADV_CRC_INIT;
    RADIO->BASE0 = BLE_ADV_ACCESS_BASE0;
    RADIO->PREFIX0 = BLE_ADV_ACCESS_PREFIX0;
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    NRF_TIMER10_S->TASKS_STOP = 1u;
    radio_constlat_exit();
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Connection engine, CENTRAL role (L3 two-board harness)                    */
/*---------------------------------------------------------------------------*/
/*
 * The debug oracle: a TikuOS central so an L3 connection can be driven
 * board-to-board with microsecond ground truth on both consoles instead
 * of a black-box phone.  As master, WE choose the CONNECT_IND parameters
 * (WinOffset small + fixed => deterministic anchor; WinSize large +
 * supervision long => lenient, so the link establishes while the
 * peripheral's timing is still being tuned) and WE define the event
 * cadence, so nothing has to be predicted.  A central event is the
 * mirror of the peripheral's: TX an empty PDU first, hardware-T_IFS
 * turnaround to RX the peripheral's response.
 */

/* Connection parameters we impose as master (chosen for easy bring-up). */
#define CEN_AA        0x71764129ul
#define CEN_CRCINIT   0x00555555ul
#define CEN_HOP       7u
#define CEN_INTERVAL  24u        /* 1.25ms units -> 30 ms                  */
#define CEN_WINSIZE   10u        /* 12.5 ms transmit window (lenient)      */
#define CEN_WINOFFSET 1u         /* 1.25 ms (small, predictable anchor)    */
#define CEN_TIMEOUT   400u       /* 10ms units -> 4 s supervision (lenient)*/

static uint8_t cen_rxb[48] __attribute__((aligned(4)));
static uint8_t cen_txb[36] __attribute__((aligned(4)));
uint32_t tiku_radio_arch_dbg_cen_tifs;   /* measured peripheral T_IFS, us  */

/* One central event: TX empty PDU (our SN/NESN) on data channel @p k, then
 * hardware-T_IFS RX the peripheral's response.  Returns 2 = CRC-valid
 * response, 1 = response seen but CRC bad, 0 = no response. */
static int cen_event(uint32_t aa, uint32_t crcinit, uint8_t k,
                     tiku_radio_ll_ack_t *ack)
{
    uint32_t t_txend = 0u;
    uint8_t  crcok, got = 0u;

    uint32_t dl;

    radio_cfg_data(aa, crcinit, k);
    /* Send our pending LL control PDU (else empty) with current SN/NESN. */
    (void)ll_build_tx(cen_txb, ack);

    /* TX only (no turnaround short): we open the RX MANUALLY and EARLY
     * afterwards, not via the hardware TIFS turnaround.  The turnaround
     * opens RX at exactly TIFS=150us, but the peripheral's response T_IFS
     * measures ~117us on this silicon, so a TIFS-timed RX opens 33us AFTER
     * the response preamble and misses it entirely.  Opening manually
     * right after the TX disables lets us listen well before the response. */
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    RADIO->PACKETPTR = (uint32_t)cen_txb;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_TXEN = 1u;

    /* All waits are TIME-bounded (conn_now us), not spin-count. */
    dl = conn_now() + 400u;
    while ((int32_t)(conn_now() - dl) < 0 && RADIO->EVENTS_PHYEND == 0u) {
    }
    NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
    t_txend = NRF_TIMER10_S->CC[1];
    dl = conn_now() + 100u;
    while ((int32_t)(conn_now() - dl) < 0 && RADIO->EVENTS_DISABLED == 0u) {
    }
    RADIO->PACKETPTR = (uint32_t)cen_rxb;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;    /* clear our own TX's stale events     */
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    RADIO->TASKS_RXEN = 1u;

    /* Peripheral responds T_IFS later; our RX is already listening. */
    dl = conn_now() + 600u;
    while ((int32_t)(conn_now() - dl) < 0) {
        if (RADIO->EVENTS_ADDRESS != 0u) {
            got = 1u;
            break;
        }
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    if (!got) {
        RADIO->SHORTS = (1u << 0) | (1u << 19);
        RADIO->TASKS_DISABLE = 1u;
        dl = conn_now() + 200u;
        while ((int32_t)(conn_now() - dl) < 0 &&
               RADIO->EVENTS_DISABLED == 0u) {
        }
        return 0;
    }
    /* Peripheral T_IFS: its ADDRESS is 40us (preamble+AA) after its first
     * bit; T_IFS = (ADDRESS - our TX end) - 40. */
    {
        uint32_t t_addr;
        NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
        t_addr = NRF_TIMER10_S->CC[1];
        if ((t_addr - t_txend) > 40u && (t_addr - t_txend) < 1000u) {
            tiku_radio_arch_dbg_cen_tifs = (t_addr - t_txend) - 40u;
        }
    }
    /* Wait for the CRC verdict.  It lands well AFTER PHYEND (the CRC is
     * validated post-last-bit), and that gap scales with payload length:
     * a 200 us budget covers empties/short responses but a full-length
     * data PDU (an ATT notification) times out and is misread as a CRC
     * failure, so the peer never sees the ack and retransmits forever.
     * Budget for a max-size PDU. */
    dl = conn_now() + 700u;
    while ((int32_t)(conn_now() - dl) < 0) {
        if (RADIO->EVENTS_CRCOK != 0u || RADIO->EVENTS_CRCERROR != 0u) {
            break;
        }
    }
    crcok = (RADIO->EVENTS_CRCOK != 0u) ? 1u : 0u;
    if (crcok) {
        uint8_t h = cen_rxb[0];
        uint8_t r = tiku_radio_ll_ack(ack, (uint8_t)((h >> 3) & 1u),
                                      (uint8_t)((h >> 2) & 1u),
                                      (uint8_t)(cen_rxb[1] != 0u));
        if (r & TIKU_RADIO_LL_ACKED) {
            ll_on_acked();
        }
        if (r & TIKU_RADIO_LL_NEWDATA) {
            ll_handle_rx(cen_rxb);               /* L4: control PDUs      */
        }
    }
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    dl = conn_now() + 200u;
    while ((int32_t)(conn_now() - dl) < 0 && RADIO->EVENTS_DISABLED == 0u) {
    }
    return crcok ? 2 : 1;
}

int tiku_radio_arch_central(const uint8_t *my_addr, uint32_t max_secs,
                            tiku_radio_ll_conn_stats_t *st)
{
    static uint8_t cind[48] __attribute__((aligned(4)));
    uint8_t  chmap[5] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x1Fu };
    uint8_t  chan = 0u, last_unmapped = 0u;
    uint32_t anchor, last_valid, t_ci_end = 0u, cap_deadline;
    tiku_radio_ll_ack_t ack = { 0u, 0u };
    int      connected = 0;
    uint8_t *ll;
    uint8_t  att_prev = 0u, att_wait = 0u;        /* L5 transaction retry  */

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        memset(st, 0, sizeof(*st));
        st->reason = 2u;
        st->hop = CEN_HOP;
    }

    tiku_radio_arch_init();
    radio_constlat_enter();
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = 150u;
    conn_timer_start();

    /* Pre-build the CONNECT_IND (all but AdvA): [S0][LEN][S1][InitA][AdvA]
     * [LLData].  S0=0xC5 (CONNECT_IND, TxAdd+RxAdd random). */
    cind[0] = 0xC5u;
    cind[1] = 34u;
    cind[2] = my_addr[0];                         /* erratum-49 S1 = pay0  */
    memcpy(&cind[3], my_addr, 6u);                /* InitA                 */
    ll = &cind[15];
    ll[0] = (uint8_t)CEN_AA; ll[1] = (uint8_t)(CEN_AA >> 8);
    ll[2] = (uint8_t)(CEN_AA >> 16); ll[3] = (uint8_t)(CEN_AA >> 24);
    ll[4] = (uint8_t)CEN_CRCINIT; ll[5] = (uint8_t)(CEN_CRCINIT >> 8);
    ll[6] = (uint8_t)(CEN_CRCINIT >> 16);
    ll[7] = CEN_WINSIZE;
    ll[8] = CEN_WINOFFSET; ll[9] = 0u;
    ll[10] = CEN_INTERVAL; ll[11] = 0u;
    ll[12] = 0u; ll[13] = 0u;                     /* latency               */
    ll[14] = (uint8_t)CEN_TIMEOUT; ll[15] = (uint8_t)(CEN_TIMEOUT >> 8);
    memcpy(&ll[16], chmap, 5u);
    ll[21] = (uint8_t)(CEN_HOP & 0x1Fu);          /* hop | SCA=0           */

    cap_deadline = conn_now() + max_secs * 1000000u;

    /* --- Scan for TIKU-CONN's ADV_IND, then hardware-T_IFS the CONNECT_IND --- */
    while (!connected && (int32_t)(conn_now() - cap_deadline) < 0) {
        uint32_t spin;
        tiku_watchdog_kick();

        RADIO->BASE0   = BLE_ADV_ACCESS_BASE0;
        RADIO->PREFIX0 = BLE_ADV_ACCESS_PREFIX0;
        RADIO->CRCINIT = BLE_ADV_CRC_INIT;
        RADIO->FREQUENCY = adv_freq[chan];
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
        RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 2) | (1u << 4);
        RADIO->PACKETPTR = (uint32_t)cen_rxb;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        RADIO->EVENTS_CRCERROR = 0u;
        RADIO->EVENTS_PHYEND   = 0u;
        (void)RADIO->EVENTS_DISABLED;
        RADIO->TASKS_RXEN = 1u;

        /* Listen ~8 ms for an ADV_IND. */
        for (spin = 0u; spin < 260000u; spin++) {
            if (RADIO->EVENTS_PHYEND != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_PHYEND == 0u) {
            /* nothing: cancel the pending auto-TX and rotate */
            RADIO->SHORTS = (1u << 0) | (1u << 19);
            RADIO->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            chan = (uint8_t)((chan + 1u) % 3u);
            continue;
        }
        /* Heard a packet; the DISABLED_TXEN turnaround is ramping.  If it
         * is a connectable ADV_IND named TIKU-CONN, ship the CONNECT_IND;
         * else abort the auto-TX.  (Name at cen_rxb[14] by our peripheral's
         * fixed AD layout: [02 01 06][len 09 T I K U ...].) */
        for (spin = 0u; spin < 4000u; spin++) {
            if (RADIO->EVENTS_CRCOK != 0u || RADIO->EVENTS_CRCERROR != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_CRCOK != 0u && (cen_rxb[0] & 0x0Fu) == 0x00u &&
            cen_rxb[14] == 'T' && cen_rxb[15] == 'I' &&
            cen_rxb[16] == 'K' && cen_rxb[17] == 'U') {
            memcpy(&cind[9], &cen_rxb[3], 6u);     /* AdvA from the ADV_IND */
            RADIO->PACKETPTR = (uint32_t)cind;     /* hardware TX CONNECT_IND */
            RADIO->EVENTS_DISABLED = 0u;
            for (spin = 0u; spin < 400000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
            t_ci_end = NRF_TIMER10_S->CC[1];       /* ~our CONNECT_IND end  */
            connected = 1;
        } else {
            RADIO->SHORTS = (1u << 0) | (1u << 19);
            RADIO->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        }
        chan = (uint8_t)((chan + 1u) % 3u);
    }
    if (!connected) {
        RADIO->TIFS = 0u;
        NRF_TIMER10_S->TASKS_STOP = 1u;
        RADIO->SHORTS = (1u << 0) | (1u << 19);
        radio_constlat_exit();
        return -1;
    }

    /* --- Connection loop: WE are the timing master --- */
    ll_reset();                                   /* L4/L5: LL state       */
    ll_is_central = 1u;                           /* central = ATT client  */
    ll_queue_version();                           /* initiate: send VERSION_IND */
    anchor = t_ci_end + BLE_TX_WIN_DELAY_US + (uint32_t)CEN_WINOFFSET * 1250u;
    last_valid = t_ci_end;
    for (;;) {
        uint8_t k = tiku_radio_ll_csa1_next(last_unmapped, CEN_HOP, chmap,
                                            &last_unmapped);
        int r;

        if (ll_want_term) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 3u;
            }
            break;
        }
        while ((int32_t)(conn_now() - anchor) < 0) {
            /* park to our own anchor */
        }
        r = cen_event(CEN_AA, CEN_CRCINIT, k, &ack);
        if (st != (tiku_radio_ll_conn_stats_t *)0) {
            st->events++;
            if (r == 2) {
                st->rx_ok++;
            } else if (r == 1) {
                st->addr_seen++;
            } else {
                st->missed++;
            }
        }
        if (r == 2) {
            last_valid = conn_now();
        }
        anchor += (uint32_t)CEN_INTERVAL * 1250u;  /* master cadence       */

        /* L5 transaction retry: re-send a stalled REQUEST (steps 1-3).
         * Step 4 awaits the server's pushed notification -- the server's
         * LL retransmits that until acked, so the client just waits. */
        if (att_step >= 1u && att_step <= 6u) {
            if (att_step != att_prev) {
                att_prev = att_step;
                att_wait = 0u;
            } else if (++att_wait >= 6u) {
                if (ll_tx_len == 0u) {
                    att_client_send();
                }
                att_wait = 0u;
            }
        }

        tiku_watchdog_kick();
        if ((int32_t)(conn_now() - (last_valid +
                      (uint32_t)CEN_TIMEOUT * 10000u)) >= 0) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 1u;
            }
            break;
        }
        if ((int32_t)(conn_now() - cap_deadline) >= 0) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 0u;
            }
            break;
        }
    }

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        st->ms = (conn_now() - t_ci_end) / 1000u;
        st->peer_vers = ll_peer_vers;
        st->ctrl_tx = ll_ctrl_tx;
        st->ctrl_rx = ll_ctrl_rx;
        st->att_step = att_step;
        st->att_ok = att_ok;
        st->att_readback = att_readback;
        st->att_disc = att_disc_ok;
    }
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    {
        uint32_t spin;
        for (spin = 0u; spin < 40000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
    }
    RADIO->TIFS = 0u;
    RADIO->CRCINIT = BLE_ADV_CRC_INIT;
    RADIO->BASE0 = BLE_ADV_ACCESS_BASE0;
    RADIO->PREFIX0 = BLE_ADV_ACCESS_PREFIX0;
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    NRF_TIMER10_S->TASKS_STOP = 1u;
    radio_constlat_exit();
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Extended advertising at 1M (R8.3a)                                        */
/*---------------------------------------------------------------------------*/
/*
 * One complete non-connectable non-scannable extended advertising event:
 * ADV_EXT_IND on primary channel 37 carrying ADI + AuxPtr, then
 * AUX_ADV_IND on secondary channel 20 carrying AdvA + ADI + AdvData (the
 * >31-byte payload legacy advertising cannot).  The aux timing is
 * HARDWARE-exact -- this is what R6's fabric was built for:
 *
 *   DPPI ch1: RADIO PUBLISH_READY -> TIMER10 CLEAR+START+CAPTURE[2]
 *             (the EXT_IND's READY fires at preamble start = the
 *             AuxPtr offset's t=0)
 *   DPPI ch2: TIMER10 COMPARE[0] (aux offset - TX ramp) -> RADIO TXEN
 *
 * Between the EXT_IND's DISABLED and the hardware TXEN (~400 us) the CPU
 * only reprograms FREQUENCY/DATAWHITE/PACKETPTR for the aux channel and
 * UNSUBSCRIBES the timer's CLEAR/START -- otherwise the aux packet's own
 * READY would restart the timer and the compare would fire a rogue TXEN
 * 560 us after the aux started.  CAPTURE[2] stays subscribed on the
 * free-running timer, so CC[2] records the aux packet's ACTUAL start
 * time: the on-die proof the AUX flew inside the AuxPtr window
 * (dbg_aux_us ~= 600).
 *
 * Whitening/CRC/access address are channel-formula-identical to legacy
 * advertising; secondary channel index 20 = 2446 MHz (FREQUENCY=46).
 * MAXLEN is raised for the burst and restored (the RX scan buffer is
 * 48 B -- a permanent 255 would let EasyDMA overrun it).
 */

#define EXTADV_AUX_CH_IDX     20u      /* LE channel index (2446 MHz)     */
#define EXTADV_AUX_FREQ       46u
#define EXTADV_AUX_OFFSET_US  600u     /* 20 x 30 us AuxPtr units         */
/* TXEN->READY ramp, hardware-measured via the CC[2] capture: with a
 * 40 us assumption the aux preamble started at 640 us -- 10 us outside
 * the spec window [offset, offset + 1 unit] = [600, 630].  The real
 * ramp is ~80 us; aim mid-window (615) for symmetric margin. */
#define EXTADV_TX_RAMP_US     80u
#define EXTADV_AIM_SLACK_US   15u      /* land mid-window, not on its edge */
#define EXTADV_ADI_LO         0xBCu    /* DID=0xABC, SID=0                */
#define EXTADV_ADI_HI         0x0Au
#define EXTADV_DPPI_CH_READY  1u       /* DPPIC10 channels (0 = window)   */
#define EXTADV_DPPI_CH_TXEN   2u

uint32_t tiku_radio_arch_dbg_aux_us;   /* CC[2] capture: ~600 when on-air */

int tiku_radio_arch_extadv_burst(const uint8_t *addr,
                                 const uint8_t *ad, uint8_t ad_len)
{
    static uint8_t ext_pdu[16] __attribute__((aligned(4)));
    static uint8_t aux_pdu[224] __attribute__((aligned(4)));
    uint32_t pcnf1_saved, spin;
    int rc = 0;

    if (ad_len > 200u) {
        ad_len = 200u;
    }

    /* ADV_EXT_IND: header type 7, payload = [extHdrLen=6|mode=00]
     * [flags: ADI|AuxPtr] [ADI lo hi] [AuxPtr: ch|CA=0|units=30us,
     * offset lo, offset hi|PHY=1M].  RAM carries the erratum-49 S1 dup
     * at [2]. */
    ext_pdu[0]  = 0x07u;
    ext_pdu[1]  = 7u;
    ext_pdu[2]  = 0x06u;                       /* S1 slot = payload[0]    */
    ext_pdu[3]  = 0x06u;                       /* extHdrLen 6, AdvMode 00 */
    ext_pdu[4]  = 0x18u;                       /* flags: ADI + AuxPtr     */
    ext_pdu[5]  = EXTADV_ADI_LO;
    ext_pdu[6]  = EXTADV_ADI_HI;
    ext_pdu[7]  = EXTADV_AUX_CH_IDX;           /* CA=0, units=30 us       */
    ext_pdu[8]  = (uint8_t)(EXTADV_AUX_OFFSET_US / 30u);
    ext_pdu[9]  = 0x00u;                       /* offset hi=0, PHY=1M     */

    /* AUX_ADV_IND: header type 7 + TxAdd (AdvA is random static),
     * payload = [extHdrLen=9|mode=00][flags: AdvA|ADI][AdvA 6][ADI 2]
     * [AdvData...]. */
    aux_pdu[0] = 0x47u;
    aux_pdu[1] = (uint8_t)(10u + ad_len);
    aux_pdu[2] = 0x09u;                        /* S1 slot = payload[0]    */
    aux_pdu[3] = 0x09u;                        /* extHdrLen 9, AdvMode 00 */
    aux_pdu[4] = 0x09u;                        /* flags: AdvA + ADI       */
    memcpy(&aux_pdu[5], addr, 6u);
    aux_pdu[11] = EXTADV_ADI_LO;
    aux_pdu[12] = EXTADV_ADI_HI;
    if (ad_len) {
        memcpy(&aux_pdu[13], ad, ad_len);
    }

    radio_constlat_enter();                    /* erratum 20 bracket      */
    radio_xo_observe();
    radio_hfclk_kick();
    pcnf1_saved = RADIO->PCNF1;
    RADIO->PCNF1 = (pcnf1_saved & ~0xFFul) | 220u;     /* MAXLEN up       */

    /* Wire the aux schedule (TIMER10 free-runs at 1 MHz; no shorts --
     * with no CLEAR the compare fires exactly once). */
    NRF_TIMER10_S->TASKS_STOP = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->MODE      = 0u;
    NRF_TIMER10_S->BITMODE   = 3u;
    NRF_TIMER10_S->PRESCALER = 4u;             /* 1 us units              */
    NRF_TIMER10_S->SHORTS    = 0u;
    NRF_TIMER10_S->CC[0] = EXTADV_AUX_OFFSET_US + EXTADV_AIM_SLACK_US
                           - EXTADV_TX_RAMP_US;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR      = EXTADV_DPPI_CH_READY | (1u << 31);
    NRF_TIMER10_S->SUBSCRIBE_START      = EXTADV_DPPI_CH_READY | (1u << 31);
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[2] = EXTADV_DPPI_CH_READY | (1u << 31);
    RADIO->PUBLISH_READY                = EXTADV_DPPI_CH_READY | (1u << 31);
    NRF_TIMER10_S->PUBLISH_COMPARE[0]   = EXTADV_DPPI_CH_TXEN  | (1u << 31);
    RADIO->SUBSCRIBE_TXEN               = EXTADV_DPPI_CH_TXEN  | (1u << 31);
    NRF_DPPIC10_S->CHENSET = (1u << EXTADV_DPPI_CH_READY) |
                             (1u << EXTADV_DPPI_CH_TXEN);

    /* ADV_EXT_IND on primary channel 37. */
    RADIO->FREQUENCY = adv_freq[0];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[0]);
    RADIO->PACKETPTR = (uint32_t)ext_pdu;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_READY    = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_TXEN = 1u;
    for (spin = 0u; spin < 400000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    if (RADIO->EVENTS_DISABLED == 0u) {
        rc = -1;
    } else {
        /* ~400 us until the hardware TXEN: break the READY->restart
         * loop (the aux's own READY must NOT re-clear the timer), then
         * point the radio at the aux channel/PDU. */
        NRF_TIMER10_S->SUBSCRIBE_CLEAR = 0u;
        NRF_TIMER10_S->SUBSCRIBE_START = 0u;
        RADIO->FREQUENCY = EXTADV_AUX_FREQ;
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | EXTADV_AUX_CH_IDX);
        RADIO->PACKETPTR = (uint32_t)aux_pdu;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_READY    = 0u;
        (void)RADIO->EVENTS_DISABLED;
        /* AUX_ADV_IND flies at COMPARE[0] -- pure hardware from here. */
        for (spin = 0u; spin < 1000000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_DISABLED == 0u) {
            rc = -2;                           /* aux never flew          */
        }
        tiku_radio_arch_dbg_aux_us = NRF_TIMER10_S->CC[2];
    }

    /* Teardown: no subscription may outlive the burst (a stale
     * SUBSCRIBE_TXEN would let any later TIMER10 use fire the radio). */
    RADIO->PUBLISH_READY  = 0u;
    RADIO->SUBSCRIBE_TXEN = 0u;
    NRF_TIMER10_S->PUBLISH_COMPARE[0]   = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR      = 0u;
    NRF_TIMER10_S->SUBSCRIBE_START      = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[2] = 0u;
    NRF_DPPIC10_S->CHENCLR = (1u << EXTADV_DPPI_CH_READY) |
                             (1u << EXTADV_DPPI_CH_TXEN);
    NRF_TIMER10_S->TASKS_STOP = 1u;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    RADIO->PCNF1 = pcnf1_saved;
    radio_constlat_exit();
    return rc;
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
