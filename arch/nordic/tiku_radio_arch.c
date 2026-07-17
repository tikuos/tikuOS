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
 * On receiving a CRC-valid Data PDU (rx_sn, rx_nesn):
 *   - ACK of MY transmission: rx_nesn != my sn  =>  peer moved past my
 *     SN, so my PDU landed -> flip sn, load the next TX PDU.  Equal =>
 *     NAK, retransmit the SAME PDU.
 *   - NEW data from peer: rx_sn == my nesn  =>  not a retransmission ->
 *     deliver payload, flip nesn.  Unequal => a resend I already have
 *     -> discard payload (the ack half above still applies).
 * The subtle correctness point both flips are INDEPENDENT: a single PDU
 * can simultaneously ack my TX and carry new data.
 */

uint8_t tiku_radio_ll_ack(tiku_radio_ll_ack_t *a, uint8_t rx_sn,
                          uint8_t rx_nesn)
{
    uint8_t r = 0u;

    if ((rx_nesn & 1u) != a->sn) {             /* my last PDU acked        */
        a->sn ^= 1u;
        r |= TIKU_RADIO_LL_ACKED;
    }
    if ((rx_sn & 1u) == a->nesn) {             /* genuinely new payload    */
        a->nesn ^= 1u;
        r |= TIKU_RADIO_LL_NEWDATA;
    }
    return r;
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
