/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_radio_arch.c - nRF54L 2.4 GHz RADIO: advertising, scanning, probes.
 *
 * Legacy and extended advertising, an IRQ-driven observer scan, PHY probes
 * and an RF test carrier on the MDK register map; connections are the
 * FLPR's (arch/nordic/flpr).  The register and errata notes below apply.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK registers, NRF_RADIO_S  */
#include <arch/nordic/tiku_nordic_core.h>      /* NVIC + WFE for the IRQ scan */
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND      */
#include <kernel/timers/tiku_clock.h>          /* wall-clock scan bound       */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during a long scan     */
#include <arch/nordic/flpr/tiku_flpr_ipc.h>     /* DLE max octets             */
#include <string.h>

/*
 * nRF54L register facts that differ from the nRF52, whose offsets and
 * encodings do not carry over:
 *   - PACKETPTR is at 0xED0 (nRF52: 0x504).
 *   - Whitening is DATAWHITE (POLY bits 25:16 + IV bits 8:0), not the nRF52
 *     DATAWHITEIV-only register; the reset value 0x00890040 already carries
 *     the BLE polynomial 0x89, so per channel only the IV = 0x40|index is ORed.
 *   - TXPOWER is an enumerated code (+8 dBm = 0x03F), not signed dBm.
 *   - Interrupts are in split banks (INTENSET00/01/10/11).  TX is polled; the
 *     observer scan is IRQ-driven (RADIO_0 = IRQn 138, bank 00, priority 4,
 *     below the htimer, console and tick, so scanning never costs console
 *     bytes).
 *   - PHYEND (not END) is the "last bit on air" event for BLE 1M.
 *   - RXADDRESSES resets to 0: RX matches nothing until logical address 0
 *     is enabled.
 *
 * Silicon errata this driver works around (nRF54L15 errata sheet 4503_401,
 * all present on Rev 1 and Rev 2):
 *
 *   Erratum 49 ("First bits of on-air packet are not correct"): with
 *   S1LEN=0 the radio corrupts the leading payload byte(s) on air, so every
 *   packet fails the receiver's CRC and is silently dropped, while the TX
 *   sequencer (READY/PHYEND/DISABLED, airtime) looks perfect.  Workaround
 *   (the errata's S1LEN==0 branch): include the untransmitted S1 RAM slot
 *   (PCNF0.S1INCL=Include) and duplicate the first payload byte into it --
 *   RAM layout [S0][LEN][S1=payload0][payload...], on-air format unchanged.
 *   The RX direction then also carries the S1 slot in RAM: received payload
 *   starts at buffer[3].
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
 *   at boot in tiku_cpu_freq_boot_arch.c; the trim and errata register
 *   writes of Nordic's SystemInit are in tiku_crt_early.c.
 */

#define RADIO  NRF_RADIO_S

/* BLE 1M legacy-advertising PHY/link constants (Core spec values). */
#define BLE_ADV_ACCESS_BASE0   0x89BED600UL   /* access addr 0x8E89BED6:      */
#define BLE_ADV_ACCESS_PREFIX0 0x0000008EUL   /*   PREFIX||BASE, BALEN=3      */
#define BLE_ADV_CRC_POLY       0x0100065BUL  /* x^24+x^10+x^9+x^6+x^4+x^3+x+1 */
#define BLE_ADV_CRC_INIT       0x00555555UL   /* advertising CRC init         */
#define BLE_WHITE_POLY         0x00890000UL   /* DATAWHITE POLY field (0x89)  */

/* TXPOWER takes an enumerated code, not signed dBm (+8 dBm = 0x03F, 0 dBm =
 * 0x018).  The MDK's dBm-named codes below are the same on L15, LM20A and
 * LM20B.  A dBm value not in the table is rejected, not rounded. */
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
static const uint8_t adv_freq[3] = { 2u, 26u, 80u }; /* 2402/2426/2480 MHz */
static const uint8_t adv_index[3] = { 37u, 38u, 39u };

/* Erratum-20 bracket: hold the MCU domain in Constant Latency for the
 * duration of any radio operation, then release to Low Power.
 *
 * A duty-cycled beacon also holds it across the whole session
 * (tiku_radio_arch_constlat_hold()), so the sleeps between bursts run in
 * constant-latency mode; while held, the per-burst exit does nothing.  A
 * burst after a sleep also needs radio_hfclk_kick() below to be decodable. */
static uint8_t radio_constlat_held;

/** @brief Enter Constant Latency (erratum 20) before a radio operation. */
static void radio_constlat_enter(void)
{
    NRF_POWER_S->TASKS_CONSTLAT = 1u;
}

/** @brief Return to Low Power, unless a session hold is active. */
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

/* XO/PLL observation only; this file starts and stops no clock at run time.
 * Each such change is a hazard: a runtime TASKS_XOSTART without PLLSTART
 * wedges the device (erratum 39), a per-burst TASKS_PLLSTART puts the burst
 * inside a relock transient, and PLLSTART+XOTUNE at session start makes
 * later sessions unreliable.  No passive signal shows a missing HF clock
 * request: EVENTS_XOSTARTED does not re-fire across tickless sleeps, XO.STAT
 * stays Running and READY still fires.  radio_hfclk_kick() makes the
 * request; this observer only feeds the dbg counters. */
uint32_t tiku_radio_arch_dbg_xo_stat, tiku_radio_arch_dbg_xo_wait;
uint32_t tiku_radio_arch_dbg_xo_restarts;

/** @brief Latch XO.STAT and count an XO found restarted or stopped. */
static void radio_xo_observe(void)
{
    uint32_t stat = NRF_CLOCK_S->XO.STAT;

    tiku_radio_arch_dbg_xo_stat = stat;
    if (NRF_CLOCK_S->EVENTS_XOSTARTED != 0u || (stat & (1ul << 16)) == 0u) {
        tiku_radio_arch_dbg_xo_restarts++;     /* XO restarted or stopped    */
    }
}

void tiku_radio_arch_init(void)
{
    /* Modulation + packet format (configured once; per packet only
     * FREQUENCY, DATAWHITE, PACKETPTR). */
    RADIO->MODE = 3u;                          /* Ble_1Mbit                   */

    /* PDU layout: 1-byte S0 (the PDU header), 8-bit LENGTH, S1LEN=0 but
     * S1INCL=Include -- the erratum-49 workaround RAM slot (see the errata
     * notes above); 8-bit preamble.  MAXLEN = the DLE max, so an L2CAP PDU
     * of that size fits one LL PDU; 3-byte base address, little-endian,
     * whitening on. */
    RADIO->PCNF0 = (8u << 0) | (1u << 8) | (0u << 16) |
                   (1u << 20) | (0u << 24);
    RADIO->PCNF1 = (TIKU_FLPR_DLE_MAX_OCTETS << 0) | (0u << 8) | (3u << 16) |
                   (0u << 24) | (1u << 25);

    /* Access address 0x8E89BED6 on logical address 0 -- for TX (TXADDRESS
     * selects it) and RX (RXADDRESSES is a bitmask of enabled logical
     * addresses; it resets to 0, and then the receiver matches nothing). */
    RADIO->BASE0       = BLE_ADV_ACCESS_BASE0;
    RADIO->PREFIX0     = BLE_ADV_ACCESS_PREFIX0;
    RADIO->TXADDRESS   = 0u;
    RADIO->RXADDRESSES = 1u;

    /* 24-bit CRC, computed over the PDU but not the access address. */
    RADIO->CRCCNF  = (3u << 0) | (1u << 8);    /* LEN=Three, SKIPADDR=Skip    */
    RADIO->CRCPOLY = BLE_ADV_CRC_POLY;
    RADIO->CRCINIT = BLE_ADV_CRC_INIT;

    RADIO->TXPOWER = radio_txpower_code;        /* enumerated code, not dBm   */

    /* Auto-sequence: ramp-up -> READY -> (start) -> tx -> PHYEND -> (off). */
    RADIO->SHORTS = (1u << 0) | (1u << 19);  /* READY_START | PHYEND_DISABLE */
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
                /* Applied at the next ramp-up.  The caller guarantees the
                 * RADIO answers on the secure alias: while the FLPR owns it
                 * (beacon offload) this write is a precise bus fault, and
                 * the facade reclaims the peripheral around it. */
                RADIO->TXPOWER = radio_txpower_code;
            }
            return 0;
        }
    }
    return -1;                                 /* not a silicon-legal step    */
}

int8_t tiku_radio_arch_txpower(void)
{
    return radio_txpower_dbm;
}

/* The 15.4 PHY programs this code too, so BLE, 15.4 and /sys/radio/txpower
 * share one TX power setting. */
uint32_t tiku_radio_arch_txpower_code(void)
{
    return radio_txpower_code;
}

/**
 * @brief Transmit one PDU on a single advertising channel (blocking, polled).
 *
 * Each poll also samples STATE, counting polls in TXRU (0x9) into dbg_ru_iters
 * and in TX (0xB) into dbg_tx_iters; dbg_tx_iters grows with the PDU length.
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

/**
 * @brief Request the HF clock across a burst with a TX DMA through UARTE21,
 *        whose pins stay disconnected (PSEL reset value).
 *
 * A burst after tickless sleep is undecodable unless a peripheral clock
 * request precedes it; CPU busy-waits and the CLOCK tasks do not supply it.
 */
static void radio_hfclk_kick(void)
{
    static uint8_t kick_bytes[16];
    NRF_UARTE_Type *u = NRF_UARTE21_S;

    if (u->ENABLE == 0u) {
        u->BAUDRATE = 0x01D60000u;             /* 115200                   */
        u->ENABLE   = 8u;                      /* UARTE enable code        */
    }
    /* The request must last through the burst: one released before the
     * ramp ends leaves TX undecodable.  16 bytes at 115200 baud take about
     * 1.4 ms, longer than a 3-channel burst.  The DMA runs concurrently,
     * with no wait, and completes on its own. */
    u->EVENTS_DMA.TX.END   = 0u;
    u->DMA.TX.PTR          = (uint32_t)kick_bytes;
    u->DMA.TX.MAXCNT       = sizeof(kick_bytes);
    u->TASKS_DMA.TX.START  = 1u;
    tiku_radio_arch_dbg_xo_wait = 0u;
}

/* Public entry to the HF clock kick for the 15.4 PHY, which shares this
 * RADIO. */
void tiku_radio_arch_hfclk_kick(void)
{
    radio_hfclk_kick();
}

/* Decode the live RADIO.MODE for /sys/radio/mode: "ieee802154" while the
 * 15.4 PHY owns the radio, "ble-1m" at rest. */
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
    (void)pdu_len;                            /* length is byte[1] of the PDU */
    radio_constlat_enter();                    /* erratum 20: before any TXEN */
    radio_xo_observe();                       /* clock-tree dbg counters only */
    radio_hfclk_kick();                        /* peripheral clock request    */
    for (c = 0; c < 3u; c++) {
        adv_tx_one(c, pdu);
    }
    radio_constlat_exit();                     /* radio disabled again        */
}

/*---------------------------------------------------------------------------*/
/* IRQ-DRIVEN OBSERVER ENGINE                                                */
/*---------------------------------------------------------------------------*/
/*
 * The ISR owns the per-packet work: on DISABLED (the PHYEND->DISABLE short
 * fires it per received packet) it captures the packet and RSSI into a small
 * SPSC ring and re-arms RX on the next advertising channel.  The caller's
 * context drains the ring and WFEs between packets, waking on any interrupt
 * (radio, tick, console).  A silent channel never fires DISABLED: the
 * TIMER10->DPPI listen window below closes it, and the drain loop's forced
 * rotation is the fallback.
 *
 * RADIO IRQ priority is 4, below the htimer (1), console UARTE (2) and
 * tick/GPIOTE (3), so a scan cannot cost console bytes; /dev/uart/overruns
 * counts any that are lost.
 */

#define TIKU_NORDIC_IRQ_RADIO   138    /* RADIO_0 = periph 0x8A @ 0x5008A000 */
#define RADIO_INTEN00_DISABLED  (1u << 8)

/* Hardware listen windows: TIMER10 (free in GRTC-tick builds) and DPPIC10
 * channel 0, all inside the radio power domain.  COMPARE[0] publishes to the
 * channel and RADIO TASKS_DISABLE subscribes, so a silent channel closes
 * after RADIO_SCAN_WINDOW_US without the CPU; the DISABLED IRQ that follows
 * takes the same hop path as a packet end.  The COMPARE0->STOP short makes
 * each window one-shot, and every channel arm restarts the timer.  The drain
 * loop's tick rotation is a counted safety net (dbg_win_forced) that reads 0
 * while the window works.  In -DTIKU_NORDIC_TICK_TIMER10 builds TIMER10 is
 * the kernel tick, and the tick rotation alone closes silent channels. */
#if defined(TIKU_NORDIC_TICK_TIMER10)
#define RADIO_HW_WINDOW 0
#else
#define RADIO_HW_WINDOW 1
#define RADIO_SCAN_WINDOW_US  16000u    /* per-channel listen window       */
#define RADIO_DPPI_CH_WINDOW  0u        /* DPPIC10 channel (no other user) */
#endif

uint32_t tiku_radio_arch_dbg_win_hw, tiku_radio_arch_dbg_win_forced;

#if RADIO_HW_WINDOW
/** @brief Restart TIMER10 for a fresh one-shot listen window. */
static void radio_window_start(void)
{
    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER10_S->TASKS_START = 1u;
}

/**
 * @brief Set TIMER10 up as a 1 MHz one-shot and wire its COMPARE[0] to
 *        RADIO TASKS_DISABLE through DPPIC10.
 */
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

/**
 * @brief Unwire the listen window.
 *
 * @note Required at scan teardown: a live SUBSCRIBE_DISABLE lets a stale
 *       window kill a later TX burst mid-air.
 */
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

void tiku_radio_arch_scan_counts(uint32_t *isr, uint32_t *addr, uint32_t *crcok)
{
    if (isr != (uint32_t *)0) {
        *isr = scan_isr_count;
    }
    if (addr != (uint32_t *)0) {
        *addr = scan_addr_evts;
    }
    if (crcok != (uint32_t *)0) {
        *crcok = scan_crcok_evts;
    }
}
/* Every radio RX DMA target must hold [S0][LEN][S1 slot] plus PCNF1.MAXLEN
 * (TIKU_FLPR_DLE_MAX_OCTETS, 80) bytes: the RADIO writes up to MAXLEN
 * payload bytes on any address-matched reception, CRC pass or not, so a
 * smaller buffer lets ambient traffic overwrite the object that follows it. */
static uint8_t scan_rxbuf[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/** @brief Program the current scan channel and start RX (arm and hop). */
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

/**
 * @brief RADIO_0 ISR: one DISABLED per packet end, window close or forced
 *        rotation; queues a CRC-OK packet and hops to the next channel.
 *
 * The packet is copied before RX is re-armed (EasyDMA would overwrite
 * scan_rxbuf), and RSSISAMPLE is read here (latched per ADDRESS->RSSISTART;
 * the next packet overwrites it).
 */
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
            /* The stored LENGTH is what this entry holds, not what the air
             * claimed: a consumer walks the AD structures at buf[9] for
             * LENGTH-6 bytes, and an on-air 255 would send it past the
             * 48-byte entry. */
            scan_ring[scan_head].buf[1] = n;
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

/* Forced-rotation cadence: 2 ticks in TIMER10-tick builds, where it is the
 * only window; 4 ticks with the hardware window, well past its 16 ms, so a
 * forced rotation there means the window failed. */
#define RADIO_SCAN_ROT_TICKS  ((tiku_clock_time_t)(RADIO_HW_WINDOW ? 4u : 2u))
static tiku_clock_time_t scan_rot_wdl;
static uint32_t          scan_rot_seen;

/**
 * @brief Arm the RX engine without touching Constant Latency or the packet
 *        ring; shared by scan start and resume.
 *
 * A beacon borrows the radio by disarm, burst, arm; packets queued before
 * the burst are still delivered after it.
 */
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
    scan_rot_seen = scan_isr_count;           /* don't false-rotate on resume */
    scan_rot_wdl = (tiku_clock_time_t)(tiku_clock_time()
                                       + RADIO_SCAN_ROT_TICKS);
}

/**
 * @brief Disarm the RX engine and restore the TX-only SHORTS; Constant
 *        Latency and the packet ring are untouched.
 */
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
    radio_constlat_enter();                    /* erratum 20: before any RXEN */
    radio_xo_observe();                       /* clock-tree dbg counters only */

    scan_head = 0u;
    scan_tail = 0u;
    scan_addr_evts = 0u;
    scan_crcok_evts = 0u;
    scan_isr_count = 0u;
    scan_chan = 0u;
    radio_scan_arm();
}

/* Time-division borrow: hand the radio to a TX burst and take it back,
 * ring intact, Constant Latency untouched (the beacon session holds it).
 * pause() leaves the radio idle with the TX shorts that
 * tiku_radio_arch_adv_send() expects. */
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

    /* Drain everything the ISR queued.  SPSC: this function owns the tail
     * and the ISR the head; volatile ordering suffices on this one core. */
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
     * restored; ring stragglers stay queued for one more service), then
     * release the per-operation Constant Latency.  While a beacon session
     * holds CONSTLAT the exit does nothing. */
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
/* MULTI-PHY PROBE                                                           */
/*---------------------------------------------------------------------------*/

/* Per-PHY MODE + PCNF0 preamble/coded fields (MDK encodings from
 * nrf54l15_types.h): PLEN@24 (8bit=0, 16bit=1, LongRange=3), CILEN@22,
 * TERMLEN@29.  The base PCNF0 bits (LFLEN=8, S0LEN=1, erratum-49 S1INCL)
 * stay identical across PHYs.
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

/** @brief Set MODE and the PCNF0 preamble and coded fields for @p phy. */
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
/* TWO-BOARD PER-PHY LINK                                                    */
/*---------------------------------------------------------------------------*/
/*
 * TX one prepared PDU, or count received packets, at a given PHY on
 * advertising channel `chan` (0..2 = 37/38/39), on the PHY_LINK_AA access
 * address with the advertising CRC.  Neither restores 1M: the caller loops
 * for a PER run, holds Constant Latency across it (erratum 20) and calls
 * tiku_radio_arch_init() afterwards.
 */

static uint8_t phy_rxbuf[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/* Access address of the PHY link, so ambient BLE (all on the shared adv AA
 * 0x8E89BED6 at 1M) is address-rejected, not received. */
#define PHY_LINK_AA   0x71764129u

/** @brief Point logical address 0 at PHY_LINK_AA for TX and RX. */
static void radio_phy_link_aa(void)
{
    RADIO->BASE0   = (PHY_LINK_AA & 0x00FFFFFFu) << 8;
    RADIO->PREFIX0 = (PHY_LINK_AA >> 24) & 0xFFu;
    RADIO->TXADDRESS   = 0u;
    RADIO->RXADDRESSES = 1u;
}

/**
 * @brief Force the RADIO to DISABLED with SHORTS cleared: a TXEN or RXEN
 *        issued while a prior op is mid-ramp or in RXIDLE misfires.
 */
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

/*---------------------------------------------------------------------------*/
/* RF TEST CARRIER                                                           */
/*---------------------------------------------------------------------------*/
/*
 * Two classic radio test signals, on any frequency in the 2.4 GHz
 * band rather than only the advertising channels:
 *
 *   UNMODULATED  ramp TX up and stop.  The RADIO parks in TXIDLE and
 *                emits a pure carrier at FREQUENCY -- a single line on
 *                a spectrum analyser.  Used for antenna/matching work,
 *                XO trim, conducted-power checks and range rigs.
 *   MODULATED    additionally START a packet whose payload is a
 *                pseudo-random bit pattern, so the carrier carries the
 *                modulation of the selected PHY -- an occupied-bandwidth
 *                / eye-quality signal rather than a tone.
 *
 * The transmit stays on until tiku_radio_arch_carrier_stop().  This is the
 * only path in the driver that leaves the RADIO enabled across a return, so
 * the caller must stop it before any beacon, scan or connection work, which
 * all start from DISABLED.
 *
 * The ramp sequence is the one every other TX path here uses: hold
 * Constant Latency (erratum 20), observe the XO, kick HFCLK (this
 * silicon needs the UARTE21 nudge), apply the PHY, then force DISABLED
 * so a half-ramped prior op cannot make TXEN misfire.
 */

/** Non-zero while a test carrier is transmitting. */
static uint8_t radio_carrier_on;

/** Pseudo-random payload for the modulated form (not PRBS9). */
static uint8_t radio_carrier_pdu[64] __attribute__((aligned(4)));

int tiku_radio_arch_carrier_start(tiku_radio_arch_phy_t phy,
                                  uint16_t mhz, int modulated)
{
    uint32_t spin;
    uint8_t  i;

    if (mhz < 2360u || mhz > 2500u) {
        return -1;                             /* outside the tunable band */
    }
    if (radio_carrier_on) {
        tiku_radio_arch_carrier_stop();
    }

    radio_constlat_enter();
    radio_xo_observe();
    radio_hfclk_kick();
    radio_apply_phy(phy);
    radio_phy_force_disable();

    /* FREQUENCY counts MHz above 2400; MAP=1 selects the low band so
     * 2360..2399 is reachable as an offset above 2360. */
    if (mhz >= 2400u) {
        RADIO->FREQUENCY = (uint32_t)(mhz - 2400u);
    } else {
        RADIO->FREQUENCY = (uint32_t)(mhz - 2360u) |
                           (1u << RADIO_FREQUENCY_MAP_Pos);
    }
    RADIO->TXPOWER = radio_txpower_code;
    RADIO->SHORTS  = 0u;

    if (modulated) {
        /* BASE0/PREFIX0/TXADDRESS are left alone and whatever access
         * address is configured is sent: overwriting the shared adv AA
         * here makes a later scan reject every packet. */
        radio_carrier_pdu[0] = 0x42u;
        radio_carrier_pdu[1] = 37u;
        for (i = 0u; i < 38u; i++) {
            radio_carrier_pdu[2u + i] = (uint8_t)(0xA5u ^ (i * 7u));
        }
        RADIO->PACKETPTR = (uint32_t)(uintptr_t)radio_carrier_pdu;
        /* END->START keeps re-transmitting without CPU help, so the
         * modulation runs continuously rather than as one burst. */
        RADIO->SHORTS = RADIO_SHORTS_END_START_Msk;
    }

    RADIO->EVENTS_READY = 0u;
    RADIO->TASKS_TXEN   = 1u;
    for (spin = 0u; spin < 400000u; spin++) {
        if (RADIO->EVENTS_READY != 0u) {
            break;
        }
    }
    if (RADIO->EVENTS_READY == 0u) {
        radio_phy_force_disable();
        radio_constlat_exit();
        return -1;                             /* ramp-up never completed  */
    }
    RADIO->EVENTS_READY = 0u;

    if (modulated) {
        RADIO->TASKS_START = 1u;
    }

    radio_carrier_on = 1u;
    return 0;
}

void tiku_radio_arch_carrier_stop(void)
{
    if (radio_carrier_on == 0u) {
        return;
    }
    RADIO->SHORTS = 0u;
    radio_phy_force_disable();
    radio_apply_phy(TIKU_RADIO_PHY_1M);         /* beacon/scan contract    */
    radio_constlat_exit();
    radio_carrier_on = 0u;
}

int tiku_radio_arch_carrier_active(void)
{
    return (int)radio_carrier_on;
}

uint32_t tiku_radio_arch_state(void)
{
    return RADIO->STATE;
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
    RADIO->SHORTS = (1u << 0) | (1u << 19);     /* READY_START|PHYEND_DISABLE */
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_TXEN = 1u;
    /* Large cap: coded S=8 airtime is ~8x 1M, past adv_tx_one's window.
     * The watchdog is kicked inside the wait, so a stuck ramp cannot reset
     * the board. */
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
    /* Coded RX is always armed as Ble_LR125Kbit (MODE 5): FEC block 1 (the
     * header) is coded S=8 on every long-range packet, and its Coding
     * Indicator tells the receiver whether the payload is S=8 or S=2; the
     * hardware then switches rate (EVENTS_RATEBOOST: "receive mode is
     * changed from Ble_LR125Kbit to Ble_LR500Kbit", datasheet 8-radio).  RX
     * armed as MODE 6 (LR500) skips block-1 decode and misses most S=2
     * packets.  TX uses MODE 6 to send S=2. */
    radio_apply_phy((phy == TIKU_RADIO_PHY_CODED_S2)
                    ? TIKU_RADIO_PHY_CODED_S8 : phy);
    radio_phy_link_aa();
    RADIO->FREQUENCY = adv_freq[chan];
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
    RADIO->PACKETPTR = (uint32_t)phy_rxbuf;
    RADIO->SHORTS = (1u << 0);                   /* READY_START only          */
    RADIO->EVENTS_END      = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;
    radio_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;                     /* ramp once, then stay in RX */

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
            RADIO->EVENTS_RATEBOOST = 0u;
            RADIO->TASKS_START = 1u;            /* re-RX from RXIDLE, no ramp */
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
/* CSA#1 CHANNEL SELECTION                                                   */
/*---------------------------------------------------------------------------*/
/*
 * Channel Selection Algorithm #1 (Core Vol 6 Part B 4.5.8.2), the hop
 * engine every legacy connection uses:
 *   unmapped = (lastUnmapped + hopIncrement) mod 37
 *   used?    -> channel = unmapped
 *   unused?  -> channel = usedChannels[unmapped mod numUsed] (ascending)
 * lastUnmapped advances to `unmapped` either way, never to the remapped
 * channel.  `bleadv csa1` checks this function against vectors from an
 * independent implementation (three maps, including heavy remapping).
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
/* DATA-PDU ACKNOWLEDGEMENT AND FLOW CONTROL                                 */
/*---------------------------------------------------------------------------*/
/*
 * The SN/NESN 1-bit sliding window (Core Vol 6 Part B 4.5.9).  Each side
 * carries in every Data PDU header its own SN (the sequence number of the
 * PDU it sends) and NESN (the one it expects next, which acks the peer's
 * last PDU).
 *
 * On a CRC-valid Data PDU (rx_sn, rx_nesn, has_payload):
 *   - rx_nesn != local sn: the peer acked the local PDU; flip sn and load
 *     the next one.  Equal: a NAK; retransmit the same PDU.
 *   - rx_sn == local nesn: a new packet; flip nesn, and deliver it when it
 *     carries payload.  Otherwise it is a resend and is discarded; its ack
 *     half still applies.
 * The two flips are independent: one PDU can ack and carry new data.  NESN
 * advances for every new packet, an empty one included, as the spec has
 * it; a peer whose empty PDU goes unacked resends it and never sends its
 * data.  An ack then covers an empty PDU as often as a pending one, so the
 * pending slot clears only when the PDU acked was it (ll_tx_sent).
 * Notifications, which no reply confirms, are retransmitted until acked.
 */

uint8_t tiku_radio_ll_ack(tiku_radio_ll_ack_t *a, uint8_t rx_sn,
                          uint8_t rx_nesn, uint8_t has_payload)
{
    uint8_t r = 0u;

    if ((rx_nesn & 1u) != a->sn) {             /* local PDU acked          */
        a->sn ^= 1u;
        r |= TIKU_RADIO_LL_ACKED;
    }
    if ((rx_sn & 1u) == a->nesn) {             /* a new packet             */
        a->nesn ^= 1u;
        if (has_payload) {
            r |= TIKU_RADIO_LL_NEWDATA;
        }
    }
    return r;
}

/*---------------------------------------------------------------------------*/
/* EXTENDED ADVERTISING AT 1M                                                */
/*---------------------------------------------------------------------------*/
/*
 * One non-connectable, non-scannable extended advertising event:
 * ADV_EXT_IND on primary channel 37 carrying ADI + AuxPtr, then
 * AUX_ADV_IND on secondary channel 20 carrying AdvA + ADI + AdvData
 * (payloads longer than legacy advertising's 31 bytes).  The aux timing is
 * set in hardware:
 *
 *   DPPI ch1: RADIO PUBLISH_READY -> TIMER10 CLEAR+START+CAPTURE[2]
 *             (the EXT_IND's READY fires at preamble start = the
 *             AuxPtr offset's t=0)
 *   DPPI ch2: TIMER10 COMPARE[0] (aux offset - TX ramp) -> RADIO TXEN
 *
 * Between the EXT_IND's DISABLED and the hardware TXEN (~400 us) the CPU
 * reprograms FREQUENCY/DATAWHITE/PACKETPTR for the aux channel and
 * unsubscribes the timer's CLEAR/START: left subscribed, the aux packet's
 * own READY restarts the timer and the compare fires a rogue TXEN 560 us
 * after the aux starts.  CAPTURE[2] stays subscribed on the free-running
 * timer, so CC[2] records the aux packet's actual start (dbg_aux_us, about
 * 600 when it flew inside the AuxPtr window).
 *
 * Whitening, CRC and access address follow the legacy advertising formulas;
 * secondary channel index 20 is 2446 MHz (FREQUENCY=46).  MAXLEN is raised
 * to 220 for the burst and restored: RX DMA buffers are sized for the normal
 * MAXLEN, and a larger one left set lets EasyDMA overrun them.
 */

#define EXTADV_AUX_CH_IDX     20u      /* LE channel index (2446 MHz)     */
#define EXTADV_AUX_FREQ       46u
#define EXTADV_AUX_OFFSET_US  600u     /* 20 x 30 us AuxPtr units         */
/* TXEN->READY ramp, about 80 us on this part (seen in the CC[2] capture).
 * The aux preamble must start inside the spec window [offset, offset + 1
 * unit] = [600, 630] us; the compare aims mid-window, at 615. */
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
         * loop (the aux's own READY must not re-clear the timer), then
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

/*
 * An active scanner used as a yardstick: listen for an advertiser by name,
 * send it a SCAN_REQ, and capture when its SCAN_RSP comes back.  The request
 * goes out through the same timer-driven TXEN the advertiser answers with,
 * and the timer, cleared by the request's own PHYEND, is read at the reply's
 * ADDRESS: `gap` is the reply's first bit plus 40 us of preamble and AA, on
 * this radio's clock.
 */
/**
 * @brief Wait for the short's DISABLED, then a memory barrier, before a
 *        received packet is parsed.
 *
 * The DMA writes the buffer behind the compiler's back; without the barrier
 * a header byte loaded for the previous packet is reused for this one.
 */
static void rx_landed(void)
{
    uint32_t spin;
    for (spin = 0u; spin < 40000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

/* DPPIC10 channels between the RADIO and TIMER10 for the timed request. */
#define CONNADV_DPPI_CH_PHYEND 3u
#define CONNADV_DPPI_CH_ADDR   4u
#define CONNADV_DPPI_CH_TXEN   5u
/* TIMER10 ticks from an advert's end to the request's TXEN: 200 is the
 * figure a second radio measures as the 150 us T_IFS. */
uint32_t tiku_radio_arch_scanreq_txen_ticks = 200u;
uint32_t tiku_radio_arch_dbg_scanreq_adv;    /* ADV_INDs from the target   */
uint32_t tiku_radio_arch_dbg_scanreq_sent;   /* SCAN_REQs sent             */
uint32_t tiku_radio_arch_dbg_scanreq_rsp;    /* SCAN_RSPs received         */
uint32_t tiku_radio_arch_dbg_scanreq_gap_min;
uint32_t tiku_radio_arch_dbg_scanreq_gap_max;
uint32_t tiku_radio_arch_dbg_scanreq_gap_sum;
uint32_t tiku_radio_arch_dbg_scanreq_crcbad; /* replies with a bad CRC   */
uint32_t tiku_radio_arch_dbg_scanreq_wrong;  /* other packets in the window*/
uint32_t tiku_radio_arch_dbg_scanreq_silent; /* windows with nothing in   */
uint8_t  tiku_radio_arch_dbg_scanreq_pkt[3][16]; /* first 3 "wrong" ones */


int tiku_radio_arch_scanreq_probe(const uint8_t *scana, const char *name,
                                  uint32_t ms)
{
    static uint8_t req[16] __attribute__((aligned(4)));
    static uint8_t rx[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_clock_time_t span =
        (tiku_clock_time_t)((ms * (uint32_t)TIKU_CLOCK_SECOND) / 1000u);
    size_t nlen = strlen(name);
    uint8_t chan = 0u;
    uint8_t target[6];
    uint8_t have_target = 0u;

    req[0] = 0xC3u;                            /* SCAN_REQ, TxAdd=1, RxAdd=1 */
    req[1] = 12u;
    req[2] = scana[0];                         /* erratum-49 S1 slot       */
    memcpy(&req[3], scana, 6u);                /* ScanA                    */

    tiku_radio_arch_dbg_scanreq_adv = 0u;
    tiku_radio_arch_dbg_scanreq_sent = 0u;
    tiku_radio_arch_dbg_scanreq_rsp = 0u;
    tiku_radio_arch_dbg_scanreq_gap_min = 0xFFFFFFFFu;
    tiku_radio_arch_dbg_scanreq_gap_max = 0u;
    tiku_radio_arch_dbg_scanreq_gap_sum = 0u;
    tiku_radio_arch_dbg_scanreq_crcbad = 0u;
    tiku_radio_arch_dbg_scanreq_wrong = 0u;
    tiku_radio_arch_dbg_scanreq_silent = 0u;
    memset(tiku_radio_arch_dbg_scanreq_pkt, 0, sizeof tiku_radio_arch_dbg_scanreq_pkt);

    radio_constlat_enter();
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = 0u;

    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->MODE      = 0u;
    NRF_TIMER10_S->BITMODE   = 3u;
    NRF_TIMER10_S->PRESCALER = 4u;
    NRF_TIMER10_S->SHORTS    = 0u;
    NRF_TIMER10_S->CC[0] = tiku_radio_arch_scanreq_txen_ticks;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR      = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[4] = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    NRF_TIMER10_S->PUBLISH_COMPARE[0]   = CONNADV_DPPI_CH_TXEN   | (1u << 31);
    RADIO->PUBLISH_PHYEND  = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    RADIO->PUBLISH_ADDRESS = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    RADIO->SUBSCRIBE_TXEN  = CONNADV_DPPI_CH_TXEN   | (1u << 31);
    NRF_DPPIC10_S->CHENCLR = (1u << CONNADV_DPPI_CH_TXEN);
    NRF_DPPIC10_S->CHENSET = (1u << CONNADV_DPPI_CH_PHYEND) |
                             (1u << CONNADV_DPPI_CH_ADDR);
    NRF_TIMER10_S->TASKS_START = 1u;

    while ((tiku_clock_time_t)(tiku_clock_time() - t0) < span) {
        uint32_t spin;
        uint8_t type, plen, match = 0u;

        tiku_watchdog_kick();
        /* Listen on one advertising channel (a bounded poll). */
        RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
        RADIO->FREQUENCY = adv_freq[chan];
        RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | adv_index[chan]);
        RADIO->PACKETPTR = (uint32_t)rx;
        RADIO->EVENTS_PHYEND   = 0u;
        RADIO->EVENTS_END      = 0u;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
        (void)RADIO->EVENTS_PHYEND;
        RADIO->TASKS_RXEN = 1u;
        for (spin = 0u; spin < 520000u; spin++) {
            if (RADIO->EVENTS_PHYEND != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_PHYEND == 0u) {
            RADIO->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            chan = (uint8_t)((chan + 1u) % 3u);
            continue;
        }
        rx_landed();
        type = (uint8_t)(rx[0] & 0x0Fu);
        plen = rx[1];
        if (RADIO->EVENTS_CRCOK != 0u && type == 0x00u && plen >= 6u &&
            plen <= 37u) {
            /* ADV_IND: does its complete local name match @p name? */
            uint8_t i = 9u, end = (uint8_t)(3u + plen);
            while (i + 1u < end) {
                uint8_t l = rx[i], t = rx[i + 1u];
                if (l == 0u || (uint8_t)(i + 1u + l) > end) {
                    break;
                }
                if (t == 0x09u && (size_t)(l - 1u) >= nlen &&
                    memcmp(&rx[i + 2u], name, nlen) == 0) {
                    match = 1u;
                    break;
                }
                i = (uint8_t)(i + 1u + l);
            }
        }
        if (!match) {
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            continue;                          /* same channel, listen on  */
        }
        tiku_radio_arch_dbg_scanreq_adv++;
        memcpy(target, &rx[3], 6u);
        have_target = 1u;
        memcpy(&req[9], target, 6u);           /* AdvA                     */

        /* The request: the timer is running from the ADV_IND's end. */
        RADIO->PACKETPTR = (uint32_t)req;
        RADIO->EVENTS_READY = 0u;
        RADIO->EVENTS_TXREADY = 0u;
        RADIO->EVENTS_DISABLED = 0u;
        (void)RADIO->EVENTS_DISABLED;
        NRF_DPPIC10_S->CHENSET = (1u << CONNADV_DPPI_CH_TXEN);
        for (spin = 0u; spin < 100000u; spin++) {
            if (RADIO->EVENTS_READY != 0u || RADIO->EVENTS_TXREADY != 0u) {
                break;
            }
        }
        NRF_DPPIC10_S->CHENCLR = (1u << CONNADV_DPPI_CH_TXEN);
        for (spin = 0u; spin < 400000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_DISABLED == 0u) {
            RADIO->TASKS_DISABLE = 1u;
            continue;
        }
        tiku_radio_arch_dbg_scanreq_sent++;

        /* The reply: the timer restarted at the request's end.  The
         * bounded listen is wide enough for any turnaround. */
        RADIO->PACKETPTR = (uint32_t)rx;
        RADIO->EVENTS_PHYEND   = 0u;
        RADIO->EVENTS_END      = 0u;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->EVENTS_CRCOK    = 0u;
        (void)RADIO->EVENTS_PHYEND;
        RADIO->TASKS_RXEN = 1u;
        for (spin = 0u; spin < 80000u; spin++) {
            if (RADIO->EVENTS_PHYEND != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_PHYEND != 0u) {
            rx_landed();
        }
        if (RADIO->EVENTS_PHYEND != 0u && RADIO->EVENTS_CRCOK != 0u &&
            (rx[0] & 0x0Fu) == 0x04u && memcmp(&rx[3], target, 6u) == 0) {
            uint32_t gap = NRF_TIMER10_S->CC[4];
            tiku_radio_arch_dbg_scanreq_rsp++;
            if (gap < tiku_radio_arch_dbg_scanreq_gap_min) {
                tiku_radio_arch_dbg_scanreq_gap_min = gap;
            }
            if (gap > tiku_radio_arch_dbg_scanreq_gap_max) {
                tiku_radio_arch_dbg_scanreq_gap_max = gap;
            }
            tiku_radio_arch_dbg_scanreq_gap_sum += gap;
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        } else {
            if (RADIO->EVENTS_PHYEND == 0u) {
                tiku_radio_arch_dbg_scanreq_silent++;
            } else if (RADIO->EVENTS_CRCOK == 0u) {
                tiku_radio_arch_dbg_scanreq_crcbad++;
            } else {
                if (tiku_radio_arch_dbg_scanreq_wrong < 3u) {
                    memcpy(tiku_radio_arch_dbg_scanreq_pkt
                               [tiku_radio_arch_dbg_scanreq_wrong], rx, 16u);
                }
                tiku_radio_arch_dbg_scanreq_wrong++;
            }
            RADIO->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        }
        chan = (uint8_t)((chan + 1u) % 3u);
    }

    RADIO->PUBLISH_PHYEND  = 0u;
    RADIO->PUBLISH_ADDRESS = 0u;
    RADIO->SUBSCRIBE_TXEN  = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR      = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[4] = 0u;
    NRF_TIMER10_S->PUBLISH_COMPARE[0]   = 0u;
    NRF_TIMER10_S->EVENTS_COMPARE[0]    = 0u;
    NRF_DPPIC10_S->CHENCLR = (1u << CONNADV_DPPI_CH_PHYEND) |
                             (1u << CONNADV_DPPI_CH_ADDR) |
                             (1u << CONNADV_DPPI_CH_TXEN);
    NRF_TIMER10_S->TASKS_STOP = 1u;
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
    return have_target ? 0 : -1;
}

uint8_t tiku_radio_arch_scanrsp_build(uint8_t *pdu, const uint8_t *addr,
                                     const uint8_t *sd, uint8_t sd_len)
{
    uint8_t n = tiku_radio_arch_adv_build(pdu, addr, sd, sd_len);
    pdu[0] = 0x44u;                            /* SCAN_RSP, TxAdd = random   */
    return n;
}

uint8_t tiku_radio_arch_adv_build(uint8_t *pdu, const uint8_t *addr,
                                  const uint8_t *ad, uint8_t ad_len)
{
    uint8_t plen;
    if (ad_len > 31u) {
        ad_len = 31u;                          /* legacy adv AD cap           */
    }
    plen = (uint8_t)(6u + ad_len);             /* AdvA(6) + AD                */

    pdu[0] = 0x42u;                         /* ADV_NONCONN_IND, TxAdd=random */
    pdu[1] = plen;                             /* LENGTH                      */
    pdu[2] = addr[0];                          /* erratum-49 S1 slot: duplicate
                                                * of the first payload byte   */
    memcpy(&pdu[3], addr, 6u);                 /* AdvA (little-endian on air) */
    if (ad_len) {
        memcpy(&pdu[9], ad, ad_len);
    }
    return (uint8_t)(3u + plen);               /* total bytes in the buffer   */
}
