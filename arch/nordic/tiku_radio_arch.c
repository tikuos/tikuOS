/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_radio_arch.c - nRF54L 2.4 GHz RADIO: BLE link layer.
 *
 * Legacy and extended advertising, an IRQ-driven observer scan, peripheral
 * and central connection engines, PHY probes and an RF test carrier, on the
 * MDK register map.  The register and errata notes below apply to all of it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK registers, NRF_RADIO_S  */
#include <arch/nordic/tiku_nordic_core.h>      /* NVIC + WFE for the IRQ scan */
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND      */
#include <kernel/timers/tiku_clock.h>          /* wall-clock scan bound       */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during a long scan     */
#include <interfaces/bluetooth/tiku_ble_smp_pair.h> /* SMP initiator */
#include <interfaces/bluetooth/tiku_ble_bond.h>      /* durable LTK bonds */
#include <arch/nordic/tiku_crypto_arch.h>       /* AES-ECB + AES-CCM          */
#include <arch/nordic/tiku_trng_arch.h>         /* SKDm/IVm entropy           */
#include <interfaces/bluetooth/tiku_ble_enc.h>  /* encryption demo params     */
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
/* CONNECTABLE ADVERTISING AND CONNECT_IND CAPTURE                           */
/*---------------------------------------------------------------------------*/
/*
 * Transmit the advert and open an RX window on the same channel right after:
 * a central answers T_IFS=150 us after the local packet ends, with SCAN_REQ
 * or CONNECT_IND.  The TX->RX turnaround is hardware: the DISABLED_RXEN
 * short (bit 3) re-arms the receiver, which is listening well before the
 * central's 150 us mark.  The CPU swaps PACKETPTR to the RX buffer inside
 * the ~40 us ramp.
 *
 * A SCAN_REQ for the advertiser gets a SCAN_RSP.  A CONNECT_IND is captured
 * and decoded but not answered: the probe returns with its LLData.  Fully
 * polled.
 *
 * RAM layout of a captured CONNECT_IND (erratum-49 S1 slot included):
 *   [S0][LEN=34][S1][InitA 6][AdvA 6][LLData 22]
 *    0    1      2   3..8     9..14   15..36
 * LLData: AA(4) CRCInit(3) WinSize(1) WinOffset(2) Interval(2)
 *         Latency(2) Timeout(2) ChM(5) Hop:5|SCA:3 (1).
 */

/* RADIO.TIFS during the probe: it times the TX->RX turnaround after each
 * advert.  The SCAN_RSP turnaround is timed by connadv_txen_ticks below. */
uint32_t tiku_radio_arch_connadv_tifs_cfg = 150u;
/* TIMER10 ticks (us) from a SCAN_REQ's PHYEND to the TXEN that answers it.
 * RADIO.TIFS does not govern the PHYEND_DISABLE + DISABLED_TXEN chain on
 * this part: that reply leaves at the ramp's own pace, about 58 us after the
 * request, outside the 150 +/- 2 us a scanner listens in.  The timer fires
 * TXEN instead, and the 40 us fast ramp puts the first bit at T_IFS.  200 is
 * what a second radio running tiku_radio_arch_scanreq_probe() reads as
 * 150 us; this radio's own capture of the reply reads 40 ticks more.
 * Settable for trimming on hardware. */
uint32_t tiku_radio_arch_connadv_txen_ticks = 200u;
/* PDU type the probe advertises with: 0 ADV_IND, 2 ADV_NONCONN_IND, 6
 * ADV_SCAN_IND. */
uint32_t tiku_radio_arch_connadv_pdu_type;
/* TIMER10 ticks per millisecond, measured against the kernel clock at every
 * probe; the T_IFS figures here are in TIMER10 ticks. */
uint32_t tiku_radio_arch_dbg_connadv_ticks_per_ms;
uint32_t tiku_radio_arch_dbg_connadv_rxtifs;
uint32_t tiku_radio_arch_dbg_connadv_rxtifs_n;
uint32_t tiku_radio_arch_dbg_connadv_rxtifs_min;
uint32_t tiku_radio_arch_dbg_connadv_rxtifs_max;
uint32_t tiku_radio_arch_dbg_connadv_tx;      /* adverts transmitted      */
uint32_t tiku_radio_arch_dbg_connadv_scanreq; /* SCAN_REQs for this AdvA */
uint32_t tiku_radio_arch_dbg_connadv_rsp;     /* SCAN_RSPs launched       */
uint32_t tiku_radio_arch_dbg_connadv_tifs;    /* measured RX-end->TX gap  */
uint32_t tiku_radio_arch_dbg_connadv_rxother; /* CRC-OK, other AdvA       */

/* Turnaround timing.  TX->RX is the DISABLED_RXEN short, which RADIO.TIFS
 * spaces.  RX->TX runs on TIMER10: every PHYEND (DPPI ch3) clears it, every
 * ADDRESS (ch4) captures it into CC[4], and COMPARE[0] (ch5) fires the
 * SCAN_RSP's TXEN; ch5 is enabled only after a SCAN_REQ for this AdvA has
 * landed, and disabled again once TXEN fires.  CC[4] after a request reads
 * the scanner's T_IFS plus 40 us of preamble and access address; after the
 * response it reads this radio's T_IFS plus 40. */
/**
 * @brief Wait for the short's DISABLED, then a memory barrier, before a
 *        received packet is parsed.
 *
 * The DMA writes the buffer behind the compiler's back; without the barrier
 * a header byte loaded for the previous packet is reused for this one.
 */
static void connadv_landed(void)
{
    uint32_t spin;
    for (spin = 0u; spin < 40000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

#define CONNADV_DPPI_CH_PHYEND 3u
#define CONNADV_DPPI_CH_ADDR   4u
#define CONNADV_DPPI_CH_TXEN   5u

int tiku_radio_arch_connadv_probe(const uint8_t *addr, const uint8_t *ad,
                                  uint8_t ad_len, uint8_t lldata[22],
                                  uint32_t ms)
{
    static uint8_t adv[48] __attribute__((aligned(4)));
    static uint8_t rsp[48] __attribute__((aligned(4)));
    static uint8_t rx[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
    tiku_clock_time_t t0 = tiku_clock_time();
    tiku_clock_time_t span =
        (tiku_clock_time_t)((ms * (uint32_t)TIKU_CLOCK_SECOND) / 1000u);
    uint8_t chan = 0u;
    int got = 0;

    /* ADV_IND + SCAN_RSP carry separate AD fields. */
    (void)tiku_radio_arch_adv_build(adv, addr, ad, ad_len);
    adv[0] = (uint8_t)(0x40u | tiku_radio_arch_connadv_pdu_type);
    {
        uint8_t sd[3] = { 2u, 0x0Au, (uint8_t)tiku_radio_arch_txpower() };
        (void)tiku_radio_arch_scanrsp_build(rsp, addr, sd, sizeof(sd));
    }

    tiku_radio_arch_dbg_connadv_tx = 0u;
    tiku_radio_arch_dbg_connadv_scanreq = 0u;
    tiku_radio_arch_dbg_connadv_rsp = 0u;
    tiku_radio_arch_dbg_connadv_tifs = 0u;
    tiku_radio_arch_dbg_connadv_rxtifs = 0u;
    tiku_radio_arch_dbg_connadv_rxtifs_n = 0u;
    tiku_radio_arch_dbg_connadv_rxtifs_min = 0xFFFFFFFFu;
    tiku_radio_arch_dbg_connadv_rxtifs_max = 0u;
    tiku_radio_arch_dbg_connadv_rxother = 0u;

    radio_constlat_enter();                    /* erratum 20 bracket       */
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = tiku_radio_arch_connadv_tifs_cfg;

    /* TIMER10: the T_IFS clock.  Every PHYEND clears it, so a capture on the
     * next ADDRESS reads the gap between one packet's end and the next's
     * access address, and COMPARE[0] fires the reply's TXEN at a fixed
     * distance from the request's end, on a DPPI channel left disabled
     * until a request that deserves an answer has landed. */
    NRF_TIMER10_S->TASKS_STOP  = 1u;
    NRF_TIMER10_S->TASKS_CLEAR = 1u;
    NRF_TIMER10_S->MODE      = 0u;
    NRF_TIMER10_S->BITMODE   = 3u;
    NRF_TIMER10_S->PRESCALER = 4u;
    NRF_TIMER10_S->SHORTS    = 0u;
    NRF_TIMER10_S->TASKS_START = 1u;
    {   /* Its rate, against the kernel clock: four ticks, then a capture. */
        tiku_clock_time_t c0 = tiku_clock_time();
        while ((tiku_clock_time_t)(tiku_clock_time() - c0) < 1u) {
        }
        NRF_TIMER10_S->TASKS_CLEAR = 1u;
        c0 = tiku_clock_time();
        while ((tiku_clock_time_t)(tiku_clock_time() - c0) < 4u) {
        }
        NRF_TIMER10_S->TASKS_CAPTURE[5] = 1u;
        tiku_radio_arch_dbg_connadv_ticks_per_ms =
            (NRF_TIMER10_S->CC[5] * (uint32_t)TIKU_CLOCK_SECOND) / 4000u;
    }
    NRF_TIMER10_S->CC[0] = tiku_radio_arch_connadv_txen_ticks;
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR      = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    NRF_TIMER10_S->SUBSCRIBE_CAPTURE[4] = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    NRF_TIMER10_S->PUBLISH_COMPARE[0]   = CONNADV_DPPI_CH_TXEN   | (1u << 31);
    RADIO->PUBLISH_PHYEND  = CONNADV_DPPI_CH_PHYEND | (1u << 31);
    RADIO->PUBLISH_ADDRESS = CONNADV_DPPI_CH_ADDR   | (1u << 31);
    RADIO->SUBSCRIBE_TXEN  = CONNADV_DPPI_CH_TXEN   | (1u << 31);
    NRF_DPPIC10_S->CHENSET = (1u << CONNADV_DPPI_CH_PHYEND) |
                             (1u << CONNADV_DPPI_CH_ADDR);

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

        /* RX leg is ramping (hardware short).  Drop DISABLED_RXEN from
         * the shorts and hand the DMA its buffer, inside the ramp.  The
         * window is watched on PHYEND, the packet's own end, which leaves
         * the whole T_IFS to decide what to answer with. */
        RADIO->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
        RADIO->PACKETPTR = (uint32_t)rx;
        RADIO->EVENTS_PHYEND = 0u;
        RADIO->EVENTS_END = 0u;
        RADIO->EVENTS_DISABLED = 0u;
        NRF_TIMER10_S->EVENTS_COMPARE[0] = 0u;
        (void)RADIO->EVENTS_PHYEND;

        /* Listen for an answer (a bounded poll); a central answers at
         * 150 us. */
        for (spin = 0u; spin < 260000u; spin++) {
            if (RADIO->EVENTS_PHYEND != 0u) {
                break;
            }
        }
        if (RADIO->EVENTS_PHYEND == 0u) {
            RADIO->TASKS_DISABLE = 1u;         /* window idle: rotate      */
            for (spin = 0u; spin < 40000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        } else {
            uint8_t type, forus;
            connadv_landed();
            type = (uint8_t)(rx[0] & 0x0Fu);
            forus = (RADIO->EVENTS_CRCOK != 0u &&
                     memcmp(&rx[9], addr, 6u) == 0) ? 1u : 0u;

            {   /* The request's own T_IFS: its ADDRESS time counted from
                 * the advert's end, less 40 us of preamble and AA. */
                uint32_t rxgap = NRF_TIMER10_S->CC[4];
                if (forus && type == 0x03u && rx[1] == 12u &&
                    rxgap > 40u && rxgap < 4000u) {
                    uint32_t t = rxgap - 40u;
                    tiku_radio_arch_dbg_connadv_rxtifs = t;
                    tiku_radio_arch_dbg_connadv_rxtifs_n++;
                    if (t < tiku_radio_arch_dbg_connadv_rxtifs_min) {
                        tiku_radio_arch_dbg_connadv_rxtifs_min = t;
                    }
                    if (t > tiku_radio_arch_dbg_connadv_rxtifs_max) {
                        tiku_radio_arch_dbg_connadv_rxtifs_max = t;
                    }
                }
            }
            if (forus && type == 0x03u && rx[1] == 12u) {
                /* SCAN_REQ for this advertiser: the timer restarted at its
                 * end and COMPARE[0] is on its way.  Give the DMA the
                 * SCAN_RSP and open the channel that lets the compare fire
                 * TXEN; close it again once TXEN has fired, so the reply's
                 * own end can chain nothing. */
                RADIO->PACKETPTR = (uint32_t)rsp;
                RADIO->EVENTS_READY = 0u;
                RADIO->EVENTS_TXREADY = 0u;
                RADIO->EVENTS_DISABLED = 0u;
                (void)RADIO->EVENTS_DISABLED;
                NRF_DPPIC10_S->CHENSET = (1u << CONNADV_DPPI_CH_TXEN);
                tiku_radio_arch_dbg_connadv_scanreq++;
                for (spin = 0u; spin < 100000u; spin++) {
                    if (RADIO->EVENTS_READY != 0u ||
                        RADIO->EVENTS_TXREADY != 0u) {
                        break;
                    }
                }
                NRF_DPPIC10_S->CHENCLR = (1u << CONNADV_DPPI_CH_TXEN);
                for (spin = 0u; spin < 400000u; spin++) {
                    if (RADIO->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
                if (RADIO->EVENTS_DISABLED != 0u) {
                    /* CC[4] = the reply's ADDRESS since the request's end
                     * = first bit + 40 us of preamble+AA. */
                    uint32_t gap = NRF_TIMER10_S->CC[4];
                    tiku_radio_arch_dbg_connadv_rsp++;
                    if (gap > 40u && gap < 4000u) {
                        tiku_radio_arch_dbg_connadv_tifs = gap - 40u;
                    }
                } else {
                    RADIO->TASKS_DISABLE = 1u;   /* TXEN never came      */
                    for (spin = 0u; spin < 40000u; spin++) {
                        if (RADIO->EVENTS_DISABLED != 0u) {
                            break;
                        }
                    }
                }
            } else {
                /* Anything else: the packet's end already disabled the
                 * radio, and no channel is open for the compare to fire. */
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

    /* Unwire it all: a stale SUBSCRIBE_TXEN would let any later TIMER10
     * use fire the radio. */
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
/* CONNECTION ENGINE, PERIPHERAL ROLE                                        */
/*---------------------------------------------------------------------------*/
/*
 * Holds a BLE connection with a central, as a loop of connection events
 * clocked by a free-running TIMER10 (1 MHz absolute timebase):
 *
 *   advertise ADV_IND -> capture CONNECT_IND and its end time -> first
 *   anchor = end + transmitWindowDelay (1250 us) + WinOffset * 1250 -> per
 *   event { wait for the anchor, RX on the CSA#1 data channel, answer at
 *   T_IFS (hardware DISABLED_TXEN) with the pending LL or ATT PDU, else an
 *   empty one, re-sync the anchor to the packet's arrival } until the
 *   supervision timeout, a TERMINATE_IND or the caller's cap.
 *
 * Blocking and polled: the shell is parked while connected.  Re-syncing to
 * each packet's arrival absorbs drift, so later windows are narrow.
 */

#define BLE_TX_WIN_DELAY_US  1250u    /* LE 1M transmitWindowDelay        */
#define CONN_PREROLL_US      300u     /* unused: RX opens 600 us early */

/**
 * @brief Start TIMER10 as the connection's free-running 1 MHz timebase
 *        (CC[2] for now reads, CC[1] for per-packet anchor captures).
 */
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

/** @brief Return the connection timebase, in microseconds. */
static uint32_t conn_now(void)
{
    NRF_TIMER10_S->TASKS_CAPTURE[2] = 1u;
    return NRF_TIMER10_S->CC[2];
}

/**
 * @brief Map data channel index @p k (0..36) to the RADIO FREQUENCY offset
 *        (MHz - 2400), skipping the three advertising channels.
 */
static uint8_t ble_data_chan_freq(uint8_t k)
{
    return (k <= 10u) ? (uint8_t)(4u + 2u * k)
                      : (uint8_t)(28u + 2u * (k - 11u));
}

/** @brief Set access address, CRC init, frequency and whitening for @p k. */
static void radio_cfg_data(uint32_t aa, uint32_t crcinit, uint8_t k)
{
    RADIO->BASE0     = aa << 8;                /* BALEN=3: base in top 3 B  */
    RADIO->PREFIX0   = (aa >> 24) & 0xFFu;
    RADIO->CRCINIT   = crcinit & 0x00FFFFFFul;
    RADIO->FREQUENCY = ble_data_chan_freq(k);
    RADIO->DATAWHITE = BLE_WHITE_POLY | (0x40u | k);   /* data-channel IV   */
}

/*---------------------------------------------------------------------------*/
/* LL CONTROL, L2CAP AND ATT (BOTH ROLES)                                    */
/*---------------------------------------------------------------------------*/
/*
 * One pending PDU at a time (retransmitted via SN/NESN until the peer acks),
 * request/response handling for VERSION/FEATURE/PING/LENGTH/ENC/PHY, and
 * LL_UNKNOWN_RSP for any other opcode, so a probing peer gets an answer.
 * The connection engines call ll_build_tx() for what to send and
 * ll_handle_rx() for what arrived.
 */
#define LL_CONNECTION_UPDATE 0x00u
#define LL_CHANNEL_MAP_IND   0x01u
#define LL_TERMINATE_IND     0x02u
#define LL_ENC_REQ           0x03u
#define LL_ENC_RSP           0x04u
#define LL_LENGTH_REQ        0x14u
#define LL_LENGTH_RSP        0x15u
#define LL_PHY_REQ           0x16u
#define LL_PHY_RSP           0x17u
#define LL_PHY_UPDATE_IND    0x18u
#define LL_UNKNOWN_RSP       0x07u
#define LL_FEATURE_REQ       0x08u
#define LL_FEATURE_RSP       0x09u
#define LL_VERSION_IND       0x0Cu
#define LL_PING_REQ          0x12u
#define LL_PING_RSP          0x13u

static uint8_t  ll_tx[TIKU_FLPR_DLE_BUF_SIZE]; /* [hdr][len][S1][payload] */
static uint8_t  ll_tx_len;          /* payload len; 0 = none               */
static uint8_t  ll_tx_llid;         /* 2 = L2CAP data, 3 = LL control      */
static uint8_t  ll_tx_sent;         /* the last PDU sent was the pending one */
static uint8_t  ll_peer_vers;       /* peer VersNr (0 = none heard)        */
static uint8_t  ll_sent_vers;       /* VERSION_IND has been queued         */
static uint8_t  ll_want_term;       /* peer sent TERMINATE_IND             */
static uint8_t  ll_is_central;      /* role: drives ATT client vs server   */
static uint32_t ll_ctrl_tx, ll_ctrl_rx;
/* L2CAP fragmentation and recombination: an L2CAP PDU longer than the LL
 * payload spans several data PDUs.  The TX SDU goes out one fragment of up
 * to cen_dle_max bytes per acked LL PDU (LLID 2 start, 1 continuation); the
 * RX buffer recombines incoming fragments into a whole L2CAP PDU. */
#define L2_FRAG_MAX  27u            /* pre-DLE default LL data payload      */
/* Data Length Extension: raised to the negotiated max on LL_LENGTH_RSP, so
 * an L2CAP PDU up to that size fits one LL PDU. */
static uint8_t  cen_dle_max = L2_FRAG_MAX;
static uint8_t  cen_dle_sent;       /* LL_LENGTH_REQ has been queued       */
/* 72 >= the largest L2CAP PDU either side moves: the SMP Pairing Public Key
 * (65-byte SMP + 4-byte L2CAP header = 69).  ATT stays within the MTU. */
static uint8_t  cen_sdu[72];        /* outgoing L2CAP PDU being fragmented  */
static uint16_t cen_sdu_len, cen_sdu_off;
static uint8_t  cen_rc[72];         /* incoming recombination buffer        */
static uint16_t cen_rc_len, cen_rc_expect;
/* SMP pairing initiator.  When cen_test_smp is armed the central, once the
 * LL is up, drives LE-SC "Just Works" on CID 0x0006 to a shared LTK instead
 * of the NUS ATT flow.  A = InitA (this central), B = AdvA (peer). */
static uint8_t  cen_test_smp;
static uint8_t  cen_smp_started;
static uint8_t  cen_smp_ready;      /* keypair generated, Pairing Req staged */
static uint8_t  cen_smp_a[6], cen_smp_b[6];
static uint8_t  cen_smp_btype;      /* the peer's address type (its TxAdd)   */
/* Bonding: when armed, on connect the central looks the peer's AdvA up in
 * the durable bond store.  A bonded peer skips SMP and encrypts with the
 * stored LTK; an unbonded one pairs, and the LTK is stored for the next
 * reconnect. */
static uint8_t  cen_bond_mode;      /* remember/reuse the LTK across connects */
static uint8_t  cen_bonded;         /* this connection reused a stored bond   */
/* Scan-by-address: when set, the initiator connects to a specific AdvA
 * instead of matching the "TIKU" device name. */
static uint8_t  cen_target_set;
static uint8_t  cen_target_addr[6];
static uint8_t  cen_bond_stored;    /* this run's fresh LTK is saved already  */
static uint8_t  cen_bond_ltk[16];   /* the stored LTK when cen_bonded         */
/* PHY-update state (declared here: used in ll_reset/ll_handle_rx). */
static uint8_t  cen_test_phy;       /* PHY update target: 0 off, 1 2M, 2 S8  */
static uint8_t  cen_phy_rsp;        /* LL_PHY_RSP received                   */
static uint8_t  cen_phy_applied;    /* central switched its RADIO             */
static uint16_t cen_phy_survived;   /* events serviced after the switch      */
static uint8_t  cen_phy_mode_cap;   /* RADIO->MODE readback at the switch    */
static uint8_t  cen_phy_cur;        /* live PHY in-connection (0/1M 1/2M 2/S8)*/
/* Last SMP PDU sent + a stall counter: the exchange is reply-driven, so if
 * a reply is lost the initiator re-sends its last PDU to re-prompt it (the
 * responder re-emits the matching reply -- see tiku_ble_smp_pair_feed). */
static uint8_t  cen_smp_last[TIKU_BLE_SMP_PDU_MAX];
static uint8_t  cen_smp_last_len;
static uint16_t cen_smp_wait;
/* LL encryption startup (central = initiator).  After pairing, the central
 * sends LL_ENC_REQ and derives SK = e(LTK, SKDm||SKDs) from LL_ENC_RSP. */
static uint8_t  cen_enc_stage;      /* 0 idle, 1 ENC_REQ sent, 2 SK derived  */
static uint16_t cen_enc_wait;       /* stall counter for ENC_REQ retransmit  */
static uint8_t  cen_skdm[8], cen_ivm[4];
static uint8_t  cen_sk[16], cen_iv[8];
/* L2CAP signalling (CID 0x0005): a peripheral's Connection Parameter Update
 * Request sets cen_cpu_req; the central answers with an
 * LL_CONNECTION_UPDATE_IND carrying the requested interval. */
static uint8_t  cen_cpu_req;
static uint16_t cen_cpu_interval;

/* ATT over L2CAP: a minimal NUS (Nordic UART Service) data path.
 *   peripheral = server: NUS RX (write target) and NUS TX behind a CCCD;
 *                with notifications on, a write to NUS RX (up to 61 bytes
 *                kept, the ATT MTU of 64 less the 3-byte header) is echoed
 *                as a TX notification.
 *   central    = client: MTU -> GATT discovery -> enable CCCD -> write
 *                NUS_TEST_MSG to NUS RX -> await the echoed notification
 *                -> long read and long write tests.
 * Each client request is re-sent until answered; the server's LL
 * retransmits the notification until acked.  The handles below are the
 * defaults the client falls back to when discovery finds nothing. */
#define GATT_H_NUS_RX    0x0012u    /* write:  client -> server            */
#define GATT_H_NUS_TX    0x0014u    /* notification source                 */
#define GATT_H_NUS_CCCD  0x0015u    /* CCCD for the TX characteristic       */
/* 40 bytes -> ATT 43 -> L2CAP 47: two data PDUs at the 27-byte LL payload,
 * so without DLE the write and the echoed notification are fragmented. */
static const uint8_t NUS_TEST_MSG[40] = {
    'F','R','A','G','-','L','2','C','A','P','-','P','H','A','S','E',
    'C','-','0','1','2','3','4','5','6','7','8','9','-','a','b','c',
    'd','e','f','g','h','i','j','k'
};
#define NUS_TEST_LEN  ((uint8_t)sizeof(NUS_TEST_MSG))

static uint8_t  nus_rx[61];         /* server: last bytes written to RX     */
static uint8_t  nus_rx_len;
static uint8_t  nus_cccd;           /* server: TX notifications enabled bit */
static uint8_t  nus_notify[61];     /* server: pending notification payload */
static uint8_t  nus_notify_len;     /* server: >0 = queue a TX notification */
/* Client steps: 1 MTU, 2 discover services, 3 discover characteristics,
 * 4 discover the CCCD, 5 CCCD write, 6 RX write, 7 await the notification,
 * 8-9 long read, 10-12 long write, 13-14 read back, 15 done.  Discovery
 * walks the peer's GATT DB (Read By Group Type, Read By Type, Find
 * Information); the NUS operations use the discovered handles, falling back
 * to the defaults above when a step finds nothing. */
static uint8_t  att_step;
static uint8_t  att_ok;             /* client: notification echo matched    */
static uint8_t  att_readback;       /* client/server: first data byte seen  */
static uint8_t  att_disc_ok;        /* client: discovered handles as expected */
static uint16_t att_d_rx, att_d_tx, att_d_cccd; /* discovered NUS handles   */
static uint16_t att_d_next;         /* Read-By-Type iteration cursor         */
/* Long operations on the peer's GATT DB: a long read (Read Blob) of the
 * Model characteristic (handle 0x0022, 72 bytes), then a long write
 * (Prepare/Execute) of the scratch characteristic (0x0032, 70 bytes)
 * verified by reading it back.  These handles are fixed, not discovered. */
#define ATT_MODEL_H   0x0022u
#define ATT_MODEL_LEN 72u
#define ATT_SCR_H     0x0032u
#define ATT_SCR_LEN   70u
static uint8_t  att_long[80];       /* long read / readback accumulator      */
static uint16_t att_long_len;
static uint8_t  att_lread_ok;       /* Model long read matched (Read Blob)   */
static uint8_t  att_lwrite_ok;      /* scratch long write matched (readback) */

/** @brief Clear the LL, L2CAP, ATT and central state for a new connection. */
static void ll_reset(void)
{
    ll_tx_len = 0u; ll_tx_llid = 0u; ll_peer_vers = 0u; ll_sent_vers = 0u;
    ll_tx_sent = 0u;
    ll_want_term = 0u; ll_ctrl_tx = 0u; ll_ctrl_rx = 0u;
    nus_rx_len = 0u; nus_cccd = 0u; nus_notify_len = 0u;
    att_step = 0u; att_ok = 0u; att_readback = 0u;
    att_disc_ok = 0u; att_d_rx = 0u; att_d_tx = 0u; att_d_cccd = 0u;
    att_d_next = 0u;
    att_long_len = 0u; att_lread_ok = 0u; att_lwrite_ok = 0u;  /* long ops    */
    cen_sdu_len = 0u; cen_sdu_off = 0u;         /* frag/recomb                */
    cen_rc_len = 0u; cen_rc_expect = 0u;
    cen_cpu_req = 0u; cen_cpu_interval = 0u;    /* L2CAP signalling           */
    cen_smp_started = 0u; cen_smp_ready = 0u;    /* SMP not begun             */
    cen_bonded = 0u; cen_bond_stored = 0u;       /* bonding: fresh this conn  */
    cen_smp_last_len = 0u; cen_smp_wait = 0u;
    cen_enc_stage = 0u;                          /* not encrypting            */
    cen_dle_max = L2_FRAG_MAX; cen_dle_sent = 0u; /* pre-DLE                  */
    cen_phy_rsp = 0u;                            /* no LL_PHY_RSP             */
}

/** @brief Queue a raw LL payload with @p llid (1/2 L2CAP, 3 control). */
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

/** @brief Queue an LL control PDU: @p opcode then @p dlen bytes of @p data. */
static void ll_queue(uint8_t opcode, const uint8_t *data, uint8_t dlen)
{
    uint8_t p[34];
    p[0] = opcode;
    if (dlen) {
        memcpy(&p[1], data, dlen);
    }
    ll_queue_raw(3u, p, (uint8_t)(1u + dlen));
}

/**
 * @brief Queue the fragment at cen_sdu_off (LLID 2 first, 1 continuation),
 *        up to cen_dle_max bytes; ll_on_acked() advances the offset.
 */
static void cen_sdu_frag(void)
{
    uint16_t n = (uint16_t)(cen_sdu_len - cen_sdu_off);
    uint8_t  llid = (cen_sdu_off == 0u) ? 2u : 1u;
    if (n > cen_dle_max) {                        /* DLE-negotiated size      */
        n = cen_dle_max;
    }
    ll_queue_raw(llid, &cen_sdu[cen_sdu_off], (uint8_t)n);
}

/**
 * @brief Queue an L2CAP PDU on CID @p cid_lo, fragmented across data PDUs
 *        when it exceeds one; a PDU over sizeof(cen_sdu) is dropped.
 */
static void l2cap_queue(uint8_t cid_lo, const uint8_t *payload, uint8_t plen)
{
    uint16_t i, len = (uint16_t)(4u + plen);
    if (len > (uint16_t)sizeof(cen_sdu)) {
        return;
    }
    cen_sdu[0] = plen; cen_sdu[1] = 0u;         /* L2CAP length              */
    cen_sdu[2] = cid_lo; cen_sdu[3] = 0x00u;    /* CID                       */
    for (i = 0u; i < plen; i++) {
        cen_sdu[4u + i] = payload[i];
    }
    cen_sdu_len = len; cen_sdu_off = 0u;
    cen_sdu_frag();                             /* first fragment            */
}

/** @brief Queue an ATT PDU in an L2CAP frame on CID 0x0004. */
static void att_queue(const uint8_t *att, uint8_t alen)
{
    l2cap_queue(0x04u, att, alen);
}

/**
 * @brief Pop the SMP initiator's next PDU and queue it on CID 0x0006; the
 *        initiator is strict ping-pong, at most one PDU per step.
 */
static void cen_smp_pump(void)
{
    uint8_t  smp[TIKU_BLE_SMP_PDU_MAX];
    uint16_t n = tiku_ble_smp_pair_next(smp, sizeof(smp));
    if (n > 0u) {
        memcpy(cen_smp_last, smp, n);            /* keep for retransmit       */
        cen_smp_last_len = (uint8_t)n;
        cen_smp_wait = 0u;
        l2cap_queue(0x06u, smp, (uint8_t)n);
    }
}

/** @brief Feed a recombined SMP PDU (CID 0x0006) and queue the reply. */
static void cen_smp_rx(const uint8_t *smp, uint16_t len)
{
    cen_smp_wait = 0u;                           /* progress: reset the stall */
    (void)tiku_ble_smp_pair_feed(smp, len);
    cen_smp_pump();
}

/** @brief Queue LL_LENGTH_REQ with the DLE max RX/TX octets and times. */
static void cen_send_length_req(void)
{
    uint8_t d[8];
    d[0] = (uint8_t)TIKU_FLPR_DLE_MAX_OCTETS; d[1] = 0u;   /* MaxRxOctets     */
    d[2] = (uint8_t)TIKU_FLPR_DLE_MAX_TIME;
    d[3] = (uint8_t)(TIKU_FLPR_DLE_MAX_TIME >> 8);         /* MaxRxTime       */
    d[4] = (uint8_t)TIKU_FLPR_DLE_MAX_OCTETS; d[5] = 0u;   /* MaxTxOctets     */
    d[6] = (uint8_t)TIKU_FLPR_DLE_MAX_TIME;
    d[7] = (uint8_t)(TIKU_FLPR_DLE_MAX_TIME >> 8);         /* MaxTxTime       */
    ll_queue(LL_LENGTH_REQ, d, 8u);
}

/**
 * @brief Queue LL_ENC_REQ from the current SKDm/IVm (Rand and EDIV 0 for
 *        LE SC); used for the first send and for stall retransmits.
 */
static void cen_send_enc_req(void)
{
    uint8_t req[22];
    int i;
    for (i = 0; i < 10; i++) {
        req[i] = 0u;                            /* Rand[8] || EDIV[2] = 0    */
    }
    for (i = 0; i < 8; i++) {
        req[10 + i] = cen_skdm[i];              /* SKDm                      */
    }
    for (i = 0; i < 4; i++) {
        req[18 + i] = cen_ivm[i];               /* IVm                       */
    }
    ll_queue(LL_ENC_REQ, req, 22u);
}

/**
 * @brief CCM-encrypt the demo payload with the session key and queue it as an
 *        ATT Write Command to NUS RX (handle 0x0012) for the peer to decrypt.
 */
static void cen_send_enc_data(void)
{
    static const uint8_t pt[TIKU_BLE_ENC_DEMO_PT_LEN] = TIKU_BLE_ENC_DEMO_PT;
    uint8_t nonce[13], aad = TIKU_BLE_ENC_DEMO_AAD;
    uint8_t ct[TIKU_BLE_ENC_DEMO_PT_LEN], mic[4];
    uint8_t att[3u + TIKU_BLE_ENC_DEMO_PT_LEN + 4u];
    int i;

    tiku_ble_enc_nonce(nonce, 0u, 1u, cen_iv);  /* counter 0, central->periph */
    if (tiku_crypto_arch_aes_ccm_star(0, cen_sk, 16u, nonce, &aad, 1u,
                                      pt, TIKU_BLE_ENC_DEMO_PT_LEN, 4u,
                                      ct, mic) != 0) {
        return;
    }
    att[0] = 0x52u; att[1] = 0x12u; att[2] = 0x00u;   /* Write Cmd, h=0x0012  */
    for (i = 0; i < (int)TIKU_BLE_ENC_DEMO_PT_LEN; i++) {
        att[3 + i] = ct[i];
    }
    for (i = 0; i < 4; i++) {
        att[3u + TIKU_BLE_ENC_DEMO_PT_LEN + i] = mic[i];
    }
    l2cap_queue(0x04u, att, (uint8_t)(3u + TIKU_BLE_ENC_DEMO_PT_LEN + 4u));
}

/**
 * @brief Handle L2CAP signalling (CID 0x0005): accept a Connection Parameter
 *        Update Request and flag the central loop to issue the LL update.
 */
static void sig_handle(const uint8_t *l2cap, uint8_t len)
{
    const uint8_t *sig = &l2cap[4];             /* [code][id][len:2][data]   */
    if (len >= 16u && sig[0] == 0x12u) {        /* Conn Param Update Request */
        uint8_t rsp[6];
        cen_cpu_interval = (uint16_t)(sig[6] | ((uint16_t)sig[7] << 8)); /*max*/
        rsp[0] = 0x13u; rsp[1] = sig[1];        /* Response, same identifier */
        rsp[2] = 0x02u; rsp[3] = 0x00u;         /* signalling data length 2  */
        rsp[4] = 0x00u; rsp[5] = 0x00u;         /* result 0x0000 = Accepted  */
        l2cap_queue(0x05u, rsp, 6u);
        cen_cpu_req = 1u;                        /* main loop issues the IND  */
    }
}

/** @brief Queue LL_VERSION_IND and mark it sent. */
static void ll_queue_version(void)
{
    /* VersNr 0x0C (5.3), CompId 0x0059 (Nordic), SubVersNr 0x0001. */
    static const uint8_t v[5] = { 0x0Cu, 0x59u, 0x00u, 0x01u, 0x00u };
    ll_queue(LL_VERSION_IND, v, 5u);
    ll_sent_vers = 1u;
}

/**
 * @brief Build the outgoing PDU (the pending one, else empty) with SN/NESN
 *        into @p out; returns the RAM bytes, [hdr][len][S1] plus payload.
 */
static uint8_t ll_build_tx(uint8_t *out, const tiku_radio_ll_ack_t *ack)
{
    ll_tx_sent = (uint8_t)(ll_tx_len != 0u);   /* what the next ack covers */
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

/**
 * @brief The last TX was acked: clear the pending slot and, for an L2CAP
 *        fragment, advance past it and queue the next one.
 */
static void ll_on_acked(void)
{
    if (ll_tx_len != 0u) {
        uint8_t was_l2 = (uint8_t)(ll_tx_llid == 2u || ll_tx_llid == 1u);
        uint8_t acked_len = ll_tx_len;
        ll_ctrl_tx++;
        ll_tx_len = 0u;
        if (was_l2 && cen_sdu_len != 0u) {
            uint16_t n = (uint16_t)(cen_sdu_len - cen_sdu_off);
            if (n > acked_len) {
                n = acked_len;
            }
            cen_sdu_off += n;
            if (cen_sdu_off < cen_sdu_len) {
                cen_sdu_frag();                 /* next fragment             */
            } else {
                cen_sdu_len = 0u; cen_sdu_off = 0u;  /* SDU fully sent        */
            }
        }
    }
}

/**
 * @brief ATT client: queue the request for the current step.  Used for the
 *        first send, each step advance and each retransmission.
 *
 * The peer can ack a data PDU yet ignore it as a duplicate SN, so the client
 * re-sends until the response lands; every request here is idempotent.
 */
static void att_client_send(void)
{
    uint8_t  b[7];
    uint16_t h_rx   = att_d_rx   ? att_d_rx   : GATT_H_NUS_RX;
    uint16_t h_cccd = att_d_cccd ? att_d_cccd : GATT_H_NUS_CCCD;

    if (att_step == 1u) {                       /* Exchange MTU Request    */
        b[0] = 0x02u; b[1] = 64u; b[2] = 0u;    /* MTU 64: room to fragment*/
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
    } else if (att_step == 6u) {                /* write NUS RX (40-byte msg)*/
        uint8_t w[3 + NUS_TEST_LEN], i;
        w[0] = 0x12u;
        w[1] = (uint8_t)h_rx; w[2] = (uint8_t)(h_rx >> 8);
        for (i = 0u; i < NUS_TEST_LEN; i++) {
            w[3u + i] = NUS_TEST_MSG[i];
        }
        att_queue(w, (uint8_t)(3u + NUS_TEST_LEN));
    } else if (att_step == 8u) {                /* Read Model (long)         */
        att_long_len = 0u;
        b[0] = 0x0Au;
        b[1] = (uint8_t)ATT_MODEL_H; b[2] = (uint8_t)(ATT_MODEL_H >> 8);
        att_queue(b, 3u);
    } else if (att_step == 9u) {                /* Read Blob Model           */
        b[0] = 0x0Cu;
        b[1] = (uint8_t)ATT_MODEL_H; b[2] = (uint8_t)(ATT_MODEL_H >> 8);
        b[3] = (uint8_t)att_long_len; b[4] = (uint8_t)(att_long_len >> 8);
        att_queue(b, 5u);
    } else if (att_step == 10u || att_step == 11u) { /* Prepare Write scratch */
        uint8_t  w[5 + 45], i;
        uint16_t off = (att_step == 10u) ? 0u : 45u;
        uint16_t n   = (att_step == 10u) ? 45u : (uint16_t)(ATT_SCR_LEN - 45u);
        w[0] = 0x16u;
        w[1] = (uint8_t)ATT_SCR_H; w[2] = (uint8_t)(ATT_SCR_H >> 8);
        w[3] = (uint8_t)off; w[4] = (uint8_t)(off >> 8);
        for (i = 0u; i < n; i++) {
            w[5u + i] = (uint8_t)('A' + ((off + i) % 26u));
        }
        att_queue(w, (uint8_t)(5u + n));
    } else if (att_step == 12u) {               /* Execute Write (commit)    */
        b[0] = 0x18u; b[1] = 0x01u;
        att_queue(b, 2u);
    } else if (att_step == 13u) {               /* Read scratch back         */
        att_long_len = 0u;
        b[0] = 0x0Au;
        b[1] = (uint8_t)ATT_SCR_H; b[2] = (uint8_t)(ATT_SCR_H >> 8);
        att_queue(b, 3u);
    } else if (att_step == 14u) {               /* Read Blob scratch         */
        b[0] = 0x0Cu;
        b[1] = (uint8_t)ATT_SCR_H; b[2] = (uint8_t)(ATT_SCR_H >> 8);
        b[3] = (uint8_t)att_long_len; b[4] = (uint8_t)(att_long_len >> 8);
        att_queue(b, 5u);
    }
    /* step 7 awaits the server's pushed TX notification -- nothing to send */
}

/** @brief Dispatch ATT PDU @p att of @p alen bytes (client or server). */
static void att_handle(const uint8_t *att, uint8_t alen)
{
    uint8_t op = att[0];

    if (ll_is_central) {
        /* Client: MTU -> GATT discovery -> CCCD -> write RX -> await notify
         * -> long read/write.  An ATT Error (0x01) ends a discovery phase,
         * which then advances; missing handles fall back to the defaults. */
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
            att_client_send();                   /* next RdByType / FindInfo */
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
            att_step = 7u;                          /* await the notify    */
        } else if (op == 0x1Bu && att_step == 7u) { /* Handle Value Notify */
            if (alen >= 5u && att[1] == (uint8_t)h_tx &&
                att[2] == (uint8_t)(h_tx >> 8)) {
                /* The whole 40-byte message must come back, recombined
                 * from fragments when DLE is off. */
                uint8_t ok = (uint8_t)(alen == (uint8_t)(3u + NUS_TEST_LEN));
                uint8_t i;
                att_readback = att[3];              /* echoed first byte   */
                for (i = 0u; ok != 0u && i < NUS_TEST_LEN; i++) {
                    if (att[3u + i] != NUS_TEST_MSG[i]) {
                        ok = 0u;
                    }
                }
                att_ok = ok;                        /* full echo matched   */
                att_step = 8u;                      /* loopback verified   */
                att_client_send();                  /* long read           */
            }
        } else if ((op == 0x0Bu || op == 0x0Du) &&
                   att_step >= 8u && att_step <= 14u) {
            /* Read / Read Blob Response: accumulate a long value (Model read,
             * then the scratch readback), verifying the deterministic
             * pattern once the whole value is in. */
            uint8_t vn = (uint8_t)(alen - 1u), i;
            for (i = 0u; i < vn &&
                 att_long_len < (uint16_t)sizeof(att_long); i++) {
                att_long[att_long_len++] = att[1u + i];
            }
            if (att_step <= 9u) {                   /* Model long read      */
                if (att_long_len >= ATT_MODEL_LEN) {
                    uint8_t ok = (uint8_t)(att_long_len == ATT_MODEL_LEN), j;
                    for (j = 0u; ok != 0u && j < ATT_MODEL_LEN; j++) {
                        if (att_long[j] != (uint8_t)('0' + (j % 10u))) {
                            ok = 0u;
                        }
                    }
                    att_lread_ok = ok;
                    att_step = 10u;                 /* -> long write        */
                } else {
                    att_step = 9u;                  /* more Read Blob       */
                }
            } else if (att_long_len >= ATT_SCR_LEN) { /* scratch readback   */
                uint8_t ok = (uint8_t)(att_long_len == ATT_SCR_LEN), j;
                for (j = 0u; ok != 0u && j < ATT_SCR_LEN; j++) {
                    if (att_long[j] != (uint8_t)('A' + (j % 26u))) {
                        ok = 0u;
                    }
                }
                att_lwrite_ok = ok;
                att_step = 15u;                     /* all done             */
            } else {
                att_step = 14u;                     /* more Read Blob       */
            }
            if (att_step != 15u) {
                att_client_send();
            }
        } else if (op == 0x17u && (att_step == 10u || att_step == 11u)) {
            att_step = (att_step == 10u) ? 11u : 12u;  /* Prepare Write Rsp */
            att_client_send();
        } else if (op == 0x19u && att_step == 12u) { /* Execute Write Rsp   */
            att_long_len = 0u;
            att_step = 13u;                          /* read the value back */
            att_client_send();
        }
    } else {
        /* Server: MTU; CCCD and RX writes; an RX write queues a notify. */
        if (op == 0x02u) {                          /* Exchange MTU Req    */
            uint8_t m[3] = { 0x03u, 64u, 0u };      /* MTU Rsp, 64         */
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

/**
 * @brief Server: queue a pending NUS TX notification when the slot is free.
 *
 * The LL retransmits it until acked (NESN advances only on payload).  Called
 * from the peripheral connection loop between events.
 */
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

/**
 * @brief Start the application phase once the LL setup is done: SMP pairing
 *        if armed (or LL encryption for a bonded peer), else the ATT client.
 *
 * A call after the phase has started does nothing.
 */
static void cen_kick_app(void)
{
    if (!ll_is_central || att_step != 0u || cen_smp_started) {
        return;
    }
    if (cen_test_smp) {
        if (cen_bond_mode &&
            tiku_ble_bond_find(cen_smp_b, cen_smp_btype, cen_bond_ltk)) {
            /* Known peer: skip pairing, go straight to LL encryption with
             * the stored LTK (the encryption block below fires on
             * cen_bonded without waiting for an SMP DONE). */
            cen_bonded = 1u;
            cen_smp_started = 1u;       /* don't kick the pairing exchange  */
            return;
        }
        if (!cen_smp_ready) {           /* no keypair: generate it here   */
            (void)tiku_ble_smp_pair_start(TIKU_BLE_SMP_ROLE_INITIATOR,
                                          cen_smp_a, 1u, cen_smp_b,
                                          cen_smp_btype);
            cen_smp_ready = 1u;
        }
        cen_smp_started = 1u;
        cen_smp_pump();                 /* send the staged Pairing Request  */
    } else {
        att_step = 1u;                  /* start ATT (MTU Request)         */
        att_client_send();
    }
}

/**
 * @brief Process a new received PDU (RAM [hdr][len][S1][payload], payload
 *        at buf[3]); may queue a response.
 */
static void ll_handle_rx(const uint8_t *buf)
{
    uint8_t llid = buf[0] & 0x03u;
    uint8_t op;

    if (buf[1] == 0u) {
        return;                     /* empty PDU                           */
    }
    if (llid == 0x02u || llid == 0x01u) {
        /* L2CAP fragment: recombine ([len(2)][CID(2)][payload] across data
         * PDUs, LLID 2 start / 1 continuation) then dispatch a whole PDU. */
        uint8_t plen = buf[1], i;
        if (llid == 0x02u) {
            cen_rc_len = 0u; cen_rc_expect = 0u;
            if (plen >= 2u) {
                cen_rc_expect = (uint16_t)(buf[3] |
                                           ((uint16_t)buf[4] << 8)) + 4u;
                if (cen_rc_expect > (uint16_t)sizeof(cen_rc)) {
                    cen_rc_expect = (uint16_t)sizeof(cen_rc);
                }
            }
        }
        for (i = 0u; i < plen && cen_rc_len < (uint16_t)sizeof(cen_rc); i++) {
            cen_rc[cen_rc_len++] = buf[3u + i];
        }
        if (cen_rc_expect >= 4u && cen_rc_len >= cen_rc_expect &&
            cen_rc[3] == 0x00u) {
            if (cen_rc[2] == 0x04u) {            /* ATT                       */
                ll_ctrl_rx++;
                att_handle(&cen_rc[4], (uint8_t)(cen_rc_expect - 4u));
            } else if (cen_rc[2] == 0x05u) {     /* L2CAP signalling          */
                ll_ctrl_rx++;
                sig_handle(cen_rc, (uint8_t)cen_rc_expect);
            } else if (cen_rc[2] == 0x06u) {     /* SMP pairing (CID 0x0006)  */
                ll_ctrl_rx++;
                cen_smp_rx(&cen_rc[4], (uint16_t)(cen_rc_expect - 4u));
            }
            cen_rc_len = 0u; cen_rc_expect = 0u;
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
            ll_queue_version();     /* reply with the local version        */
        } else if (ll_is_central) {
            /* DLE is negotiated only for the ATT data path.  SMP pairing
             * stays on 27-byte fragments: on top of the P-256 keygen load,
             * DLE's extra round trip and larger PDUs collapse the link, so
             * pairing starts at once. */
            if (cen_test_smp) {
                cen_kick_app();     /* SMP now, no DLE                      */
            } else if (!cen_dle_sent) {
                cen_send_length_req();  /* data path: negotiate DLE first   */
                cen_dle_sent = 1u;
            }
        }
        break;
    case LL_LENGTH_RSP:             /* raise the fragment size              */
        if (buf[1] >= 5u) {
            uint16_t peer_rx = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
            uint8_t eff = (peer_rx < (uint16_t)TIKU_FLPR_DLE_MAX_OCTETS)
                        ? (uint8_t)peer_rx : (uint8_t)TIKU_FLPR_DLE_MAX_OCTETS;
            cen_dle_max = (eff < L2_FRAG_MAX) ? L2_FRAG_MAX : eff;
        }
        cen_kick_app();             /* DLE done -> start SMP / ATT          */
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
    case LL_ENC_RSP:                /* SKDs/IVs -> session key              */
        if (buf[1] >= 13u && cen_enc_stage == 1u) {
            uint8_t skd[16], ltk[16];
            int i;
            for (i = 0; i < 8; i++) {
                skd[i] = cen_skdm[i];            /* SKD = SKDm || SKDs       */
                skd[8 + i] = buf[4 + i];         /* SKDs at buf[4..11]        */
            }
            if (cen_bonded) {
                memcpy(ltk, cen_bond_ltk, 16);   /* stored bond LTK          */
            } else {
                (void)tiku_ble_smp_pair_ltk(ltk);
            }
            (void)tiku_crypto_arch_aes_ecb(0, ltk, 16u, skd, cen_sk);
            for (i = 0; i < 4; i++) {
                cen_iv[i] = cen_ivm[i];
                cen_iv[4 + i] = buf[12 + i];     /* IVs at buf[12..15]        */
            }
            cen_enc_stage = 2u;                  /* SK ready                 */
        }
        break;
    case LL_PHY_RSP:                /* peer's PHYs: the IND can go out      */
        cen_phy_rsp = 1u;
        break;
    case LL_FEATURE_RSP:
    case LL_PING_RSP:
    case LL_UNKNOWN_RSP:
        break;                      /* responses: only counted             */
    default:
        ll_queue(LL_UNKNOWN_RSP, &op, 1u);   /* decline gracefully         */
        break;
    }
}

/* First CRC-failed packet's RAM bytes, for the whitening/CRC diagnostic. */
static uint8_t conn_fail_snap[5];
static uint8_t conn_fail_have;

/**
 * @brief One peripheral connection event: RX the central's PDU on data
 *        channel @p k, then answer at T_IFS (READY_START | PHYEND_DISABLE |
 *        DISABLED_TXEN) with the pending PDU, else an empty one.
 *
 * @p deadline bounds the RX wait (absolute TIMER10 us).  On a return of 1 or
 * 2, *anchor holds the packet's ADDRESS time.
 *
 * @return 2 CRC-valid packet, 1 packet seen with a bad CRC, 0 nothing heard
 */
static int conn_event(uint32_t aa, uint32_t crcinit, uint8_t k,
                      tiku_radio_ll_ack_t *ack, uint32_t deadline,
                      uint32_t *anchor)
{
    static uint8_t rxb[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
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

    /* Wait for the CRC verdict, not just PHYEND: CRCOK/CRCERROR fire after
     * the last bit, so sampling at PHYEND misses the verdict of a
     * payload-bearing packet.  The T_IFS TX is already ramping; there are
     * about 150 us to stage the response before its DMA starts. */
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
        /* Advance SN/NESN only on a CRC-valid packet; on a bad CRC the
         * NESN stays put -> the response is an implicit NAK -> the
         * central retransmits. */
        uint8_t h = rxb[0];
        uint8_t r = tiku_radio_ll_ack(ack, (uint8_t)((h >> 3) & 1u),
                                      (uint8_t)((h >> 2) & 1u),
                                      (uint8_t)(rxb[1] != 0u));
        if ((r & TIKU_RADIO_LL_ACKED) && ll_tx_sent) {
            ll_on_acked();                        /* the last PDU landed   */
        }
        if (r & TIKU_RADIO_LL_NEWDATA) {
            ll_handle_rx(rxb);                    /* control and L2CAP     */
        }
    }
    /* Build the response (pending LL control PDU, else empty) with the
     * updated SN/NESN.  The hardware T_IFS TX DMAs it. */
    txn = ll_build_tx(txb, ack);
    (void)txn;
    /* Drop DISABLED_TXEN now that the RX->TX turnaround has fired, keeping
     * READY_START + PHYEND_DISABLE for the response.  Left armed, the
     * response's own DISABLED re-triggers TXEN and the radio loops
     * back-to-back TX; a long PDU (an ATT notification) is then still
     * transmitting when the next event's RXEN fires, and that RX is lost. */
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

    /* --- Advertising phase: ADV_IND until a matching CONNECT_IND --- */
    {
        static uint8_t adv[48] __attribute__((aligned(4)));
        static uint8_t rx[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
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
            /* RX leg is ramping; hand it the buffer, drop the turnaround
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
    ll_reset();                                   /* LL and ATT state      */
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
        /* Pre-roll: RX opens 600 us before the anchor, so the receiver has
         * finished its ~40 us ramp when the central's packet arrives; a
         * preamble that lands during the ramp is missed.  The first window
         * also spans transmitWindowSize; later ones are narrow, since a
         * wide window catches packets far from the anchor and re-syncs
         * wrong. */
        rxen_at = anchor - 600u;
        deadline = first ? (anchor + (uint32_t)winsize * 1250u + 1000u)
                         : (anchor + 2500u);

        while ((int32_t)(conn_now() - rxen_at) < 0) {
            /* busy-wait to the RX-open time */
        }
        r = conn_event(aa, crcinit, k, &ack, deadline, &t_addr);
        att_server_pump();                    /* queue a pending notify     */
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

        if (ll_want_term) {                       /* peer terminated       */
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
/* CONNECTION ENGINE, CENTRAL ROLE                                           */
/*---------------------------------------------------------------------------*/
/*
 * A central for board-to-board connections.  It sets the CONNECT_IND
 * parameters (a small fixed WinOffset for a predictable anchor, a large
 * WinSize and a long supervision timeout) and the event cadence.  A central
 * event sends the pending PDU, else an empty one, then opens RX by hand for
 * the peripheral's response.
 */

/* Connection parameters this central imposes. */
#define CEN_AA        0x71764129ul     /* fallback if RNG can't satisfy rules */
#define CEN_CRCINIT   0x00555555ul

/**
 * @brief Draw a fresh data Access Address from the TRNG that meets the
 *        Core-spec rules (Vol 6 Part B 2.1.2); CEN_AA after 24 rejections.
 *
 * Rules: not the advertising AA, not four equal octets, no run of more than
 * 6 identical bits, at most 24 transitions, at least 2 transitions in the
 * 6 most significant bits.
 */
static uint32_t cen_gen_aa(void)
{
    uint8_t  tries;

    for (tries = 0u; tries < 24u; tries++) {
        uint8_t  b[4];
        uint32_t aa;
        uint8_t  i, run = 1u, maxrun = 1u, trans = 0u, toptrans = 0u;

        (void)tiku_trng_arch_read_bytes(b, 4);
        aa = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
             ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        if (aa == 0x8E89BED6ul) {                    /* not the adv AA        */
            continue;
        }
        if (b[0] == b[1] && b[1] == b[2] && b[2] == b[3]) {
            continue;                                /* not four equal octets */
        }
        for (i = 1u; i < 32u; i++) {
            uint8_t cur = (uint8_t)((aa >> i) & 1u);
            uint8_t prv = (uint8_t)((aa >> (i - 1u)) & 1u);
            if (cur == prv) {
                run++;
                if (run > maxrun) { maxrun = run; }
            } else {
                run = 1u;
                trans++;
                if (i >= 27u) { toptrans++; }        /* pairs in bits 26..31  */
            }
        }
        if (maxrun > 6u || trans > 24u || toptrans < 2u) {
            continue;
        }
        return aa;
    }
    return CEN_AA;
}
#define CEN_HOP       7u
#define CEN_INTERVAL  24u        /* 1.25ms units -> 30 ms                  */
#define CEN_WINSIZE   10u        /* 12.5 ms transmit window (lenient)      */
#define CEN_WINOFFSET 1u         /* 1.25 ms (small, predictable anchor)    */
#define CEN_TIMEOUT   400u       /* 10ms units -> 4 s supervision (lenient)*/

static uint8_t cen_rxb[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t cen_txb[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));
uint32_t tiku_radio_arch_dbg_cen_tifs;   /* measured peripheral T_IFS, us  */
uint32_t tiku_radio_arch_dbg_phy;        /* PHY test state, see header     */
uint32_t tiku_radio_arch_dbg_cen_aa;     /* last random central AA         */

/* When armed, the central, once the ATT loopback has completed, sends
 * LL_CHANNEL_MAP_UPDATE_IND (a reduced map), then LL_CONNECTION_UPDATE_IND
 * (a longer interval), applying each on its own side at the Instant.  A
 * peripheral that does not follow an update loses the link within a few
 * events. */
static uint8_t cen_test_updates;

void tiku_radio_arch_central_updates(uint8_t on)
{
    cen_test_updates = (on != 0u) ? 1u : 0u;
}

/* A @p target above 2 selects 2M. */
void tiku_radio_arch_central_phy(uint8_t target)
{
    cen_test_phy = (target <= 2u) ? target : 1u;
}

/* @p survived counts CRC-valid responses received after the switch. */
int tiku_radio_arch_central_phy_result(uint16_t *survived)
{
    if (survived != (uint16_t *)0) {
        *survived = cen_phy_survived;
    }
    return (cen_phy_applied != 0u) ? 1 : 0;
}

/* Arms the central as the SMP pairing initiator for the next connection
 * (CID 0x0006 instead of the NUS ATT flow). */
void tiku_radio_arch_central_smp(uint8_t on)
{
    cen_test_smp = (on != 0u) ? 1u : 0u;
}

/* Arms SMP and the durable bond store: a first connection pairs and saves
 * the LTK; a reconnect to the same peer skips SMP and reuses it. */
void tiku_radio_arch_central_bond(uint8_t on)
{
    cen_test_smp  = (on != 0u) ? 1u : 0u;
    cen_bond_mode = (on != 0u) ? 1u : 0u;
}

/* Scan-by-address: connect to a specific peer AdvA instead of by device name.
 * @p addr NULL clears the filter (back to name matching). */
void tiku_radio_arch_central_target(const uint8_t *addr)
{
    if (addr != (const uint8_t *)0) {
        memcpy(cen_target_addr, addr, 6);
        cen_target_set = 1u;
    } else {
        cen_target_set = 0u;
    }
}

/* Bonding result: 1 = this connection reused a stored bond (skipped pairing),
 * 0 = fresh pairing (or not in bond mode). */
int tiku_radio_arch_central_bonded(void)
{
    return (cen_bonded != 0u) ? 1 : 0;
}

/* Returns 1 and fills @p sk (if not NULL) once SK is derived, else 0. */
int tiku_radio_arch_central_enc(uint8_t sk[16])
{
    int i;
    if (cen_enc_stage < 2u) {
        return 0;
    }
    if (sk != (uint8_t *)0) {
        for (i = 0; i < 16; i++) {
            sk[i] = cen_sk[i];
        }
    }
    return 1;
}

/**
 * @brief One central event: TX the pending PDU, else an empty one, on data
 *        channel @p k, then open RX by hand for the peripheral's response.
 *
 * @return 2 CRC-valid response, 1 response with a bad CRC, 0 no response
 */
static int cen_event(uint32_t aa, uint32_t crcinit, uint8_t k,
                     tiku_radio_ll_ack_t *ack)
{
    uint32_t t_txend = 0u;
    uint8_t  crcok, got = 0u;

    uint32_t dl;

    radio_cfg_data(aa, crcinit, k);
    /* Send the pending LL control PDU (else empty) with current SN/NESN. */
    (void)ll_build_tx(cen_txb, ack);

    /* TX only, no turnaround short: RX is opened by hand right after the
     * TX disables.  A TIFS-timed RX opens at 150 us, after the
     * peripheral's response has started (its T_IFS is about 117 us on this
     * silicon), and misses it. */
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    RADIO->PACKETPTR = (uint32_t)cen_txb;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    (void)RADIO->EVENTS_DISABLED;
    RADIO->TASKS_TXEN = 1u;

    /* Every wait is bounded in time (conn_now, microseconds).  The PHYEND
     * budget covers a full data PDU, not only an empty one: a wait that
     * ends mid-packet mis-times the RX turnaround.  Coded S8 costs 64 us
     * per byte plus about 400 us of preamble/AA/CI/TERM framing, so every
     * budget is larger on the coded PHY (a DLE-max PDU is about 5.9 ms on
     * air). */
    dl = conn_now() + ((cen_phy_cur == 2u) ? 7000u : 900u);
    while ((int32_t)(conn_now() - dl) < 0 && RADIO->EVENTS_PHYEND == 0u) {
    }
    NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
    t_txend = NRF_TIMER10_S->CC[1];
    dl = conn_now() + ((cen_phy_cur == 2u) ? 600u : 300u);
    while ((int32_t)(conn_now() - dl) < 0 && RADIO->EVENTS_DISABLED == 0u) {
    }
    RADIO->PACKETPTR = (uint32_t)cen_rxb;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->EVENTS_ADDRESS  = 0u;    /* clear the local TX's stale events   */
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->SHORTS = (1u << 0) | (1u << 19);
    RADIO->TASKS_RXEN = 1u;

    /* Peripheral responds T_IFS later; the RX is already listening.  At S8
     * its ADDRESS lands ~490 us after the TX end (150 T_IFS + 80 preamble +
     * 256 AA), vs ~190 us at 1M -- budget accordingly. */
    dl = conn_now() + ((cen_phy_cur == 2u) ? 2000u : 600u);
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
     * bit; T_IFS = (ADDRESS - the local TX end) - 40. */
    {
        uint32_t t_addr;
        NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
        t_addr = NRF_TIMER10_S->CC[1];
        if ((t_addr - t_txend) > 40u && (t_addr - t_txend) < 1000u) {
            tiku_radio_arch_dbg_cen_tifs = (t_addr - t_txend) - 40u;
        }
    }
    /* Wait for the CRC verdict, which fires after the last bit.  The wait
     * starts at ADDRESS, so it covers a max-size PDU (about 5.9 ms on air at
     * S8): a verdict the wait misses reads as a CRC failure, and the peer,
     * never acked, retransmits forever. */
    dl = conn_now() + ((cen_phy_cur == 2u) ? 7000u : 700u);
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
        if ((r & TIKU_RADIO_LL_ACKED) && ll_tx_sent) {
            ll_on_acked();
        }
        if (r & TIKU_RADIO_LL_NEWDATA) {
            ll_handle_rx(cen_rxb);               /* control and L2CAP     */
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
    uint8_t  att_prev = 0u, att_wait = 0u;        /* ATT request retry     */
    uint8_t  dle_wait = 0u;                       /* LL_LENGTH_RSP wait    */
    /* Per-connection random Access Address and CRCInit. */
    uint32_t cen_aa = cen_gen_aa();
    uint32_t cen_crcinit;

    tiku_radio_arch_dbg_cen_aa = cen_aa;          /* observability            */

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        memset(st, 0, sizeof(*st));
        st->reason = 2u;
        st->hop = CEN_HOP;
    }
    cen_phy_applied = 0u; cen_phy_survived = 0u;  /* PHY update result        */
    cen_phy_cur = 0u;                             /* connection starts at 1M  */

    tiku_radio_arch_init();
    radio_constlat_enter();
    radio_xo_observe();
    radio_hfclk_kick();
    RADIO->TIFS = 150u;
    conn_timer_start();

    /* Pre-build the CONNECT_IND (all but AdvA): [S0][LEN][S1][InitA][AdvA]
     * [LLData].  S0=0x45 (CONNECT_IND, TxAdd random); RxAdd, bit 7, is set
     * from the advertiser's TxAdd once its ADV_IND is in. */
    cind[0] = 0x45u;
    cind[1] = 34u;
    cind[2] = my_addr[0];                         /* erratum-49 S1 = pay0  */
    memcpy(&cind[3], my_addr, 6u);                /* InitA                 */
    {   /* Fresh 24-bit CRCInit per connection too (any value is spec-legal). */
        uint8_t cb[3];
        (void)tiku_trng_arch_read_bytes(cb, 3);
        cen_crcinit = (uint32_t)cb[0] | ((uint32_t)cb[1] << 8) |
                      ((uint32_t)cb[2] << 16);
    }
    ll = &cind[15];
    ll[0] = (uint8_t)cen_aa; ll[1] = (uint8_t)(cen_aa >> 8);
    ll[2] = (uint8_t)(cen_aa >> 16); ll[3] = (uint8_t)(cen_aa >> 24);
    ll[4] = (uint8_t)cen_crcinit; ll[5] = (uint8_t)(cen_crcinit >> 8);
    ll[6] = (uint8_t)(cen_crcinit >> 16);
    ll[7] = CEN_WINSIZE;
    ll[8] = CEN_WINOFFSET; ll[9] = 0u;
    ll[10] = CEN_INTERVAL; ll[11] = 0u;
    ll[12] = 0u; ll[13] = 0u;                     /* latency               */
    ll[14] = (uint8_t)CEN_TIMEOUT; ll[15] = (uint8_t)(CEN_TIMEOUT >> 8);
    memcpy(&ll[16], chmap, 5u);
    ll[21] = (uint8_t)(CEN_HOP & 0x1Fu);          /* hop | SCA=0           */

    cap_deadline = conn_now() + max_secs * 1000000u;

    /* --- Scan for the peer's ADV_IND, then send CONNECT_IND at T_IFS --- */
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

        /* Listen for an ADV_IND (a bounded poll). */
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
        /* Heard a packet; the DISABLED_TXEN turnaround is ramping.  A
         * CRC-OK ADV_IND from the peer gets the CONNECT_IND; anything else
         * aborts the auto-TX.  The name sits at cen_rxb[14] in the
         * peripheral's fixed AD layout: [02 01 06][len 09 T I K U ...]. */
        for (spin = 0u; spin < 4000u; spin++) {
            if (RADIO->EVENTS_CRCOK != 0u || RADIO->EVENTS_CRCERROR != 0u) {
                break;
            }
        }
        /* Peer match: by AdvA when a target is set (scan-by-address), else
         * by the "TIKU" device name.  ADV_IND = PDU type 0 (S0 low
         * nibble). */
        uint8_t peer_ok = (cen_rxb[0] & 0x0Fu) == 0x00u &&
            (cen_target_set
                 ? (memcmp(&cen_rxb[3], cen_target_addr, 6) == 0)
                 : (cen_rxb[14] == 'T' && cen_rxb[15] == 'I' &&
                    cen_rxb[16] == 'K' && cen_rxb[17] == 'U'));
        if (RADIO->EVENTS_CRCOK != 0u && peer_ok) {
            /* RxAdd names AdvA's type, a public one too: a peripheral
             * ignores a CONNECT_IND whose RxAdd is not its own type. */
            cen_smp_btype = (uint8_t)((cen_rxb[0] >> 6) & 1u);
            cind[0] = (uint8_t)(0x45u | (cen_smp_btype << 7));
            memcpy(&cind[9], &cen_rxb[3], 6u);     /* AdvA from the ADV_IND */
            memcpy(cen_smp_a, my_addr, 6u);        /* SMP: A = InitA (local) */
            memcpy(cen_smp_b, &cind[9], 6u);       /*      B = AdvA (peer)   */
            RADIO->PACKETPTR = (uint32_t)cind;     /* hardware TX CONNECT_IND */
            RADIO->EVENTS_DISABLED = 0u;
            for (spin = 0u; spin < 400000u; spin++) {
                if (RADIO->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            NRF_TIMER10_S->TASKS_CAPTURE[1] = 1u;
            t_ci_end = NRF_TIMER10_S->CC[1];       /* ~the CONNECT_IND end  */
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

    /* --- Connection loop: this central sets the timing --- */
    ll_reset();                                   /* LL and ATT state      */
    ll_is_central = 1u;                           /* central = ATT client  */
    ll_queue_version();                        /* initiate: send VERSION_IND */
    anchor = t_ci_end + BLE_TX_WIN_DELAY_US + (uint32_t)CEN_WINOFFSET * 1250u;
    last_valid = t_ci_end;
    /* Update-test state: a reduced channel map, a longer interval, and a
     * small stage machine that sends each update after the loopback and
     * applies it here at the Instant, as the peripheral does. */
    {
    /* Channel-map update: drop 18 of 37 channels (keep every other one),
     * which forces CSA#1 remapping on both ends.  The peer's
     * connEventCount must match exactly at the Instant: a slip makes the
     * two sides remap to different channels and the link dies.  The
     * connection update below lengthens the interval, as phones do. */
    static const uint8_t CM_NEW[5] = { 0x55u, 0x55u, 0x55u, 0x55u, 0x15u };
    uint16_t cec = 0u;                     /* connEventCount (wraps)         */
    uint16_t cen_interval = CEN_INTERVAL;  /* current central interval      */
    uint16_t cu_new = 36u;                 /* 45 ms: exercises re-converge   */
    uint8_t  upd_stage = 0u;               /* 0 idle 1 map-sent 2 map-done   */
                                           /* 3 cu-sent 4 done               */
    uint16_t cm_instant = 0u, cu_instant = 0u, settle = 0u;
    uint8_t  sig_pend = 0u;                 /* signalling-update state        */
    uint16_t sig_instant = 0u;
    uint8_t  phy_stage = 0u;               /* 0 idle 1 req 2 ind 3 done       */
    uint16_t phy_instant = 0u, phy_at = 0u; /* Instant + cec at the switch    */
    uint8_t  phy_wait = 0u;                /* LL_PHY_RSP stall counter        */
    uint32_t phy_rxok = 0u;                /* rx_ok count at the switch       */
    for (;;) {
        uint8_t k;
        int r;

        if (ll_want_term) {
            if (st != (tiku_radio_ll_conn_stats_t *)0) {
                st->reason = 3u;
            }
            break;
        }
        /* Apply the local pending update when cec reaches the Instant.  Map
         * swap precedes this event's channel pick; the interval change takes
         * effect for the next anchor stride. */
        if (upd_stage == 1u && (uint16_t)(cec - cm_instant) < 0x8000u) {
            memcpy(chmap, CM_NEW, 5u);
            upd_stage = 2u;
            settle = cec;
        } else if (upd_stage == 3u && (uint16_t)(cec - cu_instant) < 0x8000u) {
            cen_interval = cu_new;
            upd_stage = 4u;
        }
        /* Apply a peripheral-requested interval at its Instant. */
        if (sig_pend == 1u && (uint16_t)(cec - sig_instant) < 0x8000u) {
            cen_interval = cen_cpu_interval;
            sig_pend = 2u;
        }
        /* Switch the RADIO to the target PHY at the Instant: before this
         * event's RX arm, at the same cec as the peripheral's switch.
         * MODE readback: 4 = 2M, 5 = Coded S8. */
        if (phy_stage == 2u && (uint16_t)(cec - phy_instant) < 0x8000u) {
            radio_apply_phy((cen_test_phy == 2u) ? TIKU_RADIO_PHY_CODED_S8
                                                 : TIKU_RADIO_PHY_2M);
            cen_phy_cur = cen_test_phy;                /* scale event budgets */
            cen_phy_mode_cap = (uint8_t)RADIO->MODE;
            phy_stage = 3u;
            phy_at = cec;
            phy_rxok = (st != (tiku_radio_ll_conn_stats_t *)0) ? st->rx_ok : 0u;
        }
        k = tiku_radio_ll_csa1_next(last_unmapped, CEN_HOP, chmap,
                                    &last_unmapped);
        while ((int32_t)(conn_now() - anchor) < 0) {
            /* park to the local anchor */
        }
        r = cen_event(cen_aa, cen_crcinit, k, &ack);
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
        anchor += (uint32_t)cen_interval * 1250u;  /* central cadence      */

        /* Drive the two LL updates once the loopback is done and the
         * control slot is free.  A cec+8 Instant gives the spec floor of 6
         * events of lead for the IND to be delivered and acked. */
        if (cen_test_updates && ll_tx_len == 0u) {
            if (upd_stage == 0u && att_step >= 8u) {
                /* The map update goes out right after the loopback, before
                 * the anchored RX can overshoot on a long hold. */
                uint8_t d[7];
                cm_instant = (uint16_t)(cec + 8u);
                memcpy(d, CM_NEW, 5u);
                d[5] = (uint8_t)cm_instant;
                d[6] = (uint8_t)(cm_instant >> 8);
                ll_queue(LL_CHANNEL_MAP_IND, d, 7u);
                upd_stage = 1u;
            } else if (upd_stage == 2u &&
                       (uint16_t)(cec - settle) >= 12u) {
                uint8_t d[11];
                cu_instant = (uint16_t)(cec + 8u);
                d[0] = CEN_WINSIZE;
                d[1] = 0u; d[2] = 0u;               /* WinOffset = 0         */
                d[3] = (uint8_t)cu_new; d[4] = (uint8_t)(cu_new >> 8);
                d[5] = 0u; d[6] = 0u;               /* latency 0             */
                d[7] = (uint8_t)CEN_TIMEOUT; d[8] = (uint8_t)(CEN_TIMEOUT >> 8);
                d[9] = (uint8_t)cu_instant; d[10] = (uint8_t)(cu_instant >> 8);
                ll_queue(LL_CONNECTION_UPDATE, d, 11u);
                upd_stage = 3u;
            }
        }

        /* PHY update: after the loopback, LL_PHY_REQ, then on the peer's
         * LL_PHY_RSP an LL_PHY_UPDATE_IND to 2M or Coded S8 with a cec+16
         * Instant; both sides switch RADIO MODE at the Instant (above). */
        if (cen_test_phy && ll_tx_len == 0u) {
            if (phy_stage == 0u && att_step >= 8u) {
                uint8_t d[2];
                d[0] = 0x07u; d[1] = 0x07u;      /* TX/RX_PHYS: 1M+2M+Coded  */
                ll_queue(LL_PHY_REQ, d, 2u);
                phy_stage = 1u; phy_wait = 0u;
            } else if (phy_stage == 1u && cen_phy_rsp) {
                uint8_t d[4];
                uint8_t m = (cen_test_phy == 2u) ? 0x04u : 0x02u;
                /* cec+16, above the spec floor of 6: the IND is retransmitted
                 * via SN/NESN, and a run of misses longer than the margin
                 * leaves the peripheral on 1M at the switch, a one-sided flip
                 * that kills the link. */
                phy_instant = (uint16_t)(cec + 16u);
                d[0] = m; d[1] = m;              /* C->P, P->C: 2M or Coded  */
                d[2] = (uint8_t)phy_instant;
                d[3] = (uint8_t)(phy_instant >> 8);
                ll_queue(LL_PHY_UPDATE_IND, d, 4u);
                phy_stage = 2u;                     /* wait for the Instant  */
            } else if (phy_stage == 1u && ++phy_wait >= 8u) {
                uint8_t d[2];                       /* RSP lost: re-request  */
                d[0] = 0x07u; d[1] = 0x07u;
                ll_queue(LL_PHY_REQ, d, 2u);
                phy_wait = 0u;
            }
        }

        /* A peripheral's L2CAP Connection Parameter Update Request (parsed
         * in sig_handle): issue the LL update it asked for, once the
         * Response SDU has drained and the control slot is free. */
        if (cen_cpu_req != 0u && sig_pend == 0u &&
            ll_tx_len == 0u && cen_sdu_len == 0u) {
            uint8_t d[11];
            sig_instant = (uint16_t)(cec + 8u);
            d[0] = CEN_WINSIZE;
            d[1] = 0u; d[2] = 0u;                   /* WinOffset = 0         */
            d[3] = (uint8_t)cen_cpu_interval;
            d[4] = (uint8_t)(cen_cpu_interval >> 8);
            d[5] = 0u; d[6] = 0u;                   /* latency 0             */
            d[7] = (uint8_t)CEN_TIMEOUT; d[8] = (uint8_t)(CEN_TIMEOUT >> 8);
            d[9] = (uint8_t)sig_instant; d[10] = (uint8_t)(sig_instant >> 8);
            ll_queue(LL_CONNECTION_UPDATE, d, 11u);
            sig_pend = 1u;
            cen_cpu_req = 0u;
        }

        /* Once pairing is done, or a bond is reused, drive LL encryption
         * startup (send LL_ENC_REQ, derive SK from LL_ENC_RSP), send the
         * encrypted demo write, then end the link.  A failed pairing ends
         * the link at once. */
        if (cen_test_smp &&
            (cen_bonded ||
             tiku_ble_smp_pair_state() >= TIKU_BLE_SMP_STATE_DONE)) {
            if (!cen_bonded &&
                tiku_ble_smp_pair_state() == TIKU_BLE_SMP_STATE_FAILED) {
                break;
            }
            /* A fresh pairing completed: remember the LTK so the next
             * reconnect skips SMP (bonding). */
            if (cen_bond_mode && !cen_bonded && !cen_bond_stored) {
                uint8_t ltk[16];
                if (tiku_ble_smp_pair_ltk(ltk) == 0) {
                    (void)tiku_ble_bond_store(cen_smp_b, cen_smp_btype,
                                              ltk);
                }
                cen_bond_stored = 1u;
            }
            if (cen_enc_stage == 0u && ll_tx_len == 0u && cen_sdu_len == 0u) {
                (void)tiku_trng_arch_read_bytes(cen_skdm, 8);
                (void)tiku_trng_arch_read_bytes(cen_ivm, 4);
                cen_send_enc_req();                 /* LL_ENC_REQ            */
                cen_enc_stage = 1u; cen_enc_wait = 0u;
            } else if (cen_enc_stage == 1u && ll_tx_len == 0u) {
                if (++cen_enc_wait >= 8u) {         /* stall: re-send        */
                    cen_send_enc_req();
                    cen_enc_wait = 0u;
                }
            } else if (cen_enc_stage == 2u &&
                       ll_tx_len == 0u && cen_sdu_len == 0u) {
                cen_send_enc_data();                /* CCM-encrypted write   */
                cen_enc_stage = 3u; cen_enc_wait = 0u;
            } else if (cen_enc_stage == 3u) {       /* linger so peer decrypts*/
                if (++cen_enc_wait >= 24u) {
                    if (st != (tiku_radio_ll_conn_stats_t *)0) {
                        st->reason = 0u;
                    }
                    break;
                }
            }
        }
        /* SMP stall recovery: with no reply and the TX path idle, re-send
         * the last PDU to re-prompt it; a lost final DHKey Check would
         * otherwise hang both ends. */
        if (cen_test_smp && cen_smp_last_len != 0u &&
            tiku_ble_smp_pair_state() == TIKU_BLE_SMP_STATE_PAIRING &&
            ll_tx_len == 0u && cen_sdu_len == 0u) {
            if (++cen_smp_wait >= 6u) {
                l2cap_queue(0x06u, cen_smp_last, cen_smp_last_len);
                cen_smp_wait = 0u;
            }
        }

        /* If the peer never answers LL_LENGTH_REQ (no DLE support), start
         * the app phase after a few events, on 27-byte fragments.  For a
         * DLE peer, LL_LENGTH_RSP already started it. */
        if (ll_is_central && cen_dle_sent && att_step == 0u &&
            !cen_smp_started && ll_tx_len == 0u && ++dle_wait >= 8u) {
            cen_kick_app();
            dle_wait = 0u;
        }

        /* ATT retry: re-send a stalled request (steps 1-14).  Step 7
         * awaits the server's notification, which the server's LL
         * retransmits until acked, so the client waits. */
        if (att_step >= 1u && att_step <= 14u && att_step != 7u) {
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
        cec++;                                     /* one connection event  */
    }
    if (phy_stage >= 3u) {                     /* PHY switched               */
        uint32_t final_rxok = (st != (tiku_radio_ll_conn_stats_t *)0)
                            ? st->rx_ok : 0u;
        cen_phy_applied = 1u;
        /* Survival counts responses received after the switch, not events
         * run: a dead link keeps running events but stops responding. */
        cen_phy_survived = (uint16_t)(final_rxok - phy_rxok);
    }
    tiku_radio_arch_dbg_phy = (uint32_t)phy_stage |
                              ((uint32_t)att_step << 8) |
                              ((uint32_t)cen_phy_rsp << 16) |
                              ((uint32_t)cen_phy_mode_cap << 24);
    }  /* update and PHY test scope */

    if (st != (tiku_radio_ll_conn_stats_t *)0) {
        st->ms = (conn_now() - t_ci_end) / 1000u;
        st->peer_vers = ll_peer_vers;
        st->ctrl_tx = ll_ctrl_tx;
        st->ctrl_rx = ll_ctrl_rx;
        st->att_step = att_step;
        st->att_ok = att_ok;
        st->att_readback = att_readback;
        st->att_disc = att_disc_ok;
        st->att_lread = att_lread_ok;            /* long read/write           */
        st->att_lwrite = att_lwrite_ok;
    }
    radio_apply_phy(TIKU_RADIO_PHY_1M);            /* back to 1M for scan     */
    cen_phy_cur = 0u;
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
    NRF_TIMER10_S->CC[0] = tiku_radio_arch_connadv_txen_ticks;
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
        connadv_landed();
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
            connadv_landed();
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
