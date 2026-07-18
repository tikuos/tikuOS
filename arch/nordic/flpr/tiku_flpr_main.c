/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_main.c - FLPR coprocessor firmware (F1: liveness heartbeat).
 *
 * Runs on the nRF54L15's VPR RISC-V core out of the SRAM carve.  F1 scope
 * is deliberately tiny: stamp the magic (proves the crt reached C), then
 * bump the heartbeat forever (proves steady-state life, visible to the
 * app core through /sys/flpr/heartbeat).  The pacing loop keeps the bump
 * rate in the ~kHz class so the counter is obviously moving yet reads
 * stay meaningful across a slow console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/flpr/tiku_flpr_ipc.h>
#include <arch/nordic/mdk/nrf54l15.h>   /* plain-C register structs: the
                                         * same MDK headers the M33 uses
                                         * compile unchanged for RISC-V.
                                         * This core is a NON-SECURE bus
                                         * master, so all access is via the
                                         * _NS aliases -- and only works on
                                         * peripherals the app core flipped
                                         * to NonSecure first (SPU). */

/* Outward doorbell: EVENTS_TRIGGERED[n] cannot be set from the bus (MMIO
 * writes are ignored -- measured); the VPR raises them through its VEVIF
 * CSR (VPRCSR_NORDIC_EVENTS = 0x7E2, bit n = channel n).  Only channels
 * 16..22 have INTEN bits toward the app core, so the app doorbell is
 * channel 16. */
#define FLPR_DOORBELL_CH   16u

static inline void flpr_doorbell_to_app(void)
{
    /* VEVIF EVENTS CSR (0x7E2), the documented outward-doorbell mechanism.
     * NOTE: bus MMIO on the VPR00 VEVIF register space is NOT an option
     * from this core -- it bus-faults the VPR (no trap handler => dead
     * before main; measured).  The CSR write is internal and safe either
     * way; whether the app core observes it is the open doorbell riddle
     * (see tiku_flpr_arch.c). */
    __asm__ volatile ("csrs 0x7E2, %0" :: "r"(1u << FLPR_DOORBELL_CH));
}

/* Pulse engine (F3): 50%-duty waveform on the VIO pin.
 *
 * VIO CSRs give the VPR single-cycle pin access without touching the AHB
 * GPIO block (which, as a non-secure master, it could not reach anyway):
 * DIR=0xBC1, OUT=0xBC0.
 *
 * Pacing is a calibrated down-count, not mcycle: the VPR's cycle counter
 * proved unreliable as a timebase (waits stalled even with mcountinhibit
 * cleared -- first toggle landed, second wait hung).  A busy loop is
 * fully deterministic here since this core services no interrupts.
 * FLPR_PACE_DIV converts half-period CYCLES to loop iterations and is
 * calibrated against the M33's wall clock (see /sys/flpr/pulse ms=). */
#define FLPR_PACE_DIV  10u

static void flpr_pulse(const tiku_flpr_pulse_t *p)
{
    uint32_t mask = 1u << TIKU_FLPR_VIO_BIT;
    uint32_t i;
    volatile uint32_t w;

    __asm__ volatile ("csrs 0xBC1, %0" :: "r"(mask));   /* DIR: output    */
    for (i = 0u; i < p->edges; i++) {
        if (i & 1u) {
            __asm__ volatile ("csrc 0xBC0, %0" :: "r"(mask));
        } else {
            __asm__ volatile ("csrs 0xBC0, %0" :: "r"(mask));
        }
        for (w = 0u; w < p->half_cycles / FLPR_PACE_DIV; w++) {
        }
    }
    __asm__ volatile ("csrc 0xBC0, %0" :: "r"(mask));   /* park low       */
}

/* Beacon offload (F4): one 3-channel BLE advertising burst per interval,
 * paced by the calibrated down-count, with the app core fully asleep.
 *
 * Division of labour: the M33 configured every link-config register
 * (MODE/PCNF/CRC/access-address/TXPOWER/SHORTS, erratum-49 S1 layout)
 * while the RADIO was still secure, holds CONSTLAT for the session
 * (erratum 20), and flipped RADIO+UARTE21 to NonSecure so this core can
 * reach them.  Per burst this firmware only replays the proven per-burst
 * sequence: the burst-spanning UARTE21 clock kick, then per channel
 * FREQUENCY/DATAWHITE/PACKETPTR + TXEN + poll DISABLED.  The PDU lives in
 * this core's own .bss (the NS carve), which the radio's now-non-secure
 * EasyDMA can read. */
static const uint8_t beacon_freq[3] = { 2u, 26u, 80u };
static const uint8_t beacon_widx[3] = { 37u, 38u, 39u };
static uint8_t beacon_pdu[48] __attribute__((aligned(4)));
static uint8_t beacon_kick[16];
static uint32_t beacon_pace_iters;      /* interval in pace iterations     */
static uint32_t beacon_on;

static void flpr_hfclk_kick(void)
{
    NRF_UARTE_Type *u = NRF_UARTE21_NS;

    if (u->ENABLE == 0u) {
        u->BAUDRATE = 0x01D60000u;             /* 115200: ~1.4 ms / 16 B   */
        u->ENABLE   = 8u;
    }
    u->EVENTS_DMA.TX.END  = 0u;
    u->DMA.TX.PTR         = (uint32_t)beacon_kick;
    u->DMA.TX.MAXCNT      = sizeof(beacon_kick);
    u->TASKS_DMA.TX.START = 1u;                /* concurrent, spans burst  */
}

static void flpr_beacon_burst(void)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t c, spin;

    flpr_hfclk_kick();
    for (c = 0u; c < 3u; c++) {
        r->FREQUENCY = beacon_freq[c];
        r->DATAWHITE = 0x00890000u | (0x40u | beacon_widx[c]);
        r->PACKETPTR = (uint32_t)beacon_pdu;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_READY    = 0u;
        r->TASKS_TXEN = 1u;
        for (spin = 0u; spin < 1000000u; spin++) {
            if (r->EVENTS_DISABLED != 0u) {
                break;
            }
        }
    }
}

/* RX probe (L6 F-L6.1 step 0): prove the FLPR can drive RADIO *RX*.  The
 * beacon path is TX-only; a connection controller needs RX (EasyDMA
 * WRITING into the NS carve -- unproven on this core).  Listen on adv
 * channel 37 across many short RX attempts, tally ADDRESS/CRCOK, and
 * capture the head of the first CRC-valid packet.  RXADDRESSES + BASE0 +
 * the packet format are already set by the M33 (secure) before the flip;
 * we only touch FREQUENCY/DATAWHITE/PACKETPTR/SHORTS + the RX task, mirror
 * of flpr_beacon_burst's per-channel TX. */
static uint8_t rxprobe_buf[48] __attribute__((aligned(4)));

static void flpr_rxprobe(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t addr_evts = 0u, crcok_evts = 0u, attempt, spin;
    uint8_t  got_first = 0u;

    flpr_hfclk_kick();                          /* HFCLK up for RX          */
    r->FREQUENCY = 2u;                          /* ch37 = 2402 MHz          */
    r->DATAWHITE = 0x00890000u | (0x40u | 37u);
    /* RX ramps via RXREADY; ADDRESS latches RSSI; PHYEND ends the packet
     * and disables (observer's proven RX short set).  ~1500 windows of
     * ~40k MMIO-poll iters each ≈ a few seconds of near-continuous listen. */
    for (attempt = 0u; attempt < 1500u; attempt++) {
        r->PACKETPTR = (uint32_t)rxprobe_buf;
        r->SHORTS = (1u << 0) | (1u << 4) | (1u << 18) | (1u << 19);
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_ADDRESS  = 0u;
        r->EVENTS_CRCOK    = 0u;
        r->TASKS_RXEN = 1u;
        for (spin = 0u; spin < 40000u; spin++) {
            if (r->EVENTS_DISABLED != 0u) {
                break;                          /* packet ended             */
            }
        }
        if (r->EVENTS_DISABLED == 0u) {         /* window idle: force down   */
            r->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        }
        if (r->EVENTS_ADDRESS != 0u) {
            addr_evts++;
        }
        if (r->EVENTS_CRCOK != 0u) {
            crcok_evts++;
            if (!got_first) {
                /* Copy a FIXED head, not LEN-derived: reading rxprobe_buf[1]
                 * right at CRCOK can race EasyDMA still landing the payload
                 * in RAM (LEN reads 0).  A short settle + fixed 15-byte copy
                 * captures the real head and confirms the DMA fully lands
                 * (which step 1's CONNECT_IND parse depends on). */
                uint32_t i;
                for (i = 0u; i < 200u; i++) {
                }
                for (i = 0u; i < 15u; i++) {
                    sh->rx_first[i] = rxprobe_buf[i];
                }
                sh->rx_first_len = 15u;
                got_first = 1u;
            }
        }
        if (sh->cmd != 0u) {                     /* honour STOP / new cmd     */
            break;
        }
    }
    sh->rx_addr_evts  = addr_evts;
    sh->rx_crcok_evts = crcok_evts;
    sh->rx_done = 1u;
}

/* Connection controller advertise + capture (L6 F-L6.1 step 1a): the
 * beacon's TX and the RX probe's RX joined by the hardware TX->RX
 * turnaround.  Per channel: TX the connectable ADV_IND (DISABLED_RXEN
 * short auto-opens RX), then listen for a CONNECT_IND addressed to us.
 * On a match, parse the LLData (the data-channel AA/CRCInit + timing the
 * link needs) into the conn_* shared fields.  Register sequence mirrors
 * the M33 advertising phase in tiku_radio_arch_connect; the M33 set the
 * static packet format + adv-AA/CRC before the NS flip. */
static uint8_t conn_adv[48] __attribute__((aligned(4)));
static uint8_t conn_rx[48]  __attribute__((aligned(4)));

static void flpr_conn_adv(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    const volatile tiku_flpr_conn_t *in =
        (const volatile tiku_flpr_conn_t *)sh->a2f_buf;
    uint8_t  addr[6];
    uint32_t alen = in->adv_len, i, spin, attempt;
    uint8_t  chan = 0u, connected = 0u;

    if (alen > sizeof(conn_adv)) {
        alen = sizeof(conn_adv);
    }
    for (i = 0u; i < alen; i++) {
        conn_adv[i] = in->adv[i];
    }
    for (i = 0u; i < 6u; i++) {
        addr[i] = in->addr[i];
    }
    sh->conn_state = 0u;
    sh->conn_events = 0u;
    flpr_hfclk_kick();

    for (attempt = 0u; attempt < 4000u && !connected; attempt++) {
        r->FREQUENCY = beacon_freq[chan];
        r->DATAWHITE = 0x00890000u | (0x40u | beacon_widx[chan]);
        /* TX ADV_IND, hardware turnaround to RX (DISABLED_RXEN). */
        r->SHORTS = (1u << 0) | (1u << 19) | (1u << 3) | (1u << 4);
        r->PACKETPTR = (uint32_t)conn_adv;
        r->EVENTS_DISABLED = 0u;
        (void)r->EVENTS_DISABLED;
        r->TASKS_TXEN = 1u;
        for (spin = 0u; spin < 40000u; spin++) {
            if (r->EVENTS_DISABLED != 0u) {
                break;                          /* TX done, RX ramping      */
            }
        }
        /* Hand the RX leg its buffer; drop the turnaround short. */
        r->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
        r->PACKETPTR = (uint32_t)conn_rx;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_CRCOK    = 0u;
        (void)r->EVENTS_DISABLED;
        for (spin = 0u; spin < 20000u; spin++) {
            if (r->EVENTS_DISABLED != 0u) {
                break;                          /* a packet ended           */
            }
        }
        if (r->EVENTS_DISABLED == 0u) {
            r->TASKS_DISABLE = 1u;              /* window idle: rotate      */
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        } else if (r->EVENTS_CRCOK != 0u &&
                   (conn_rx[0] & 0x0Fu) == 0x05u && conn_rx[1] == 34u) {
            uint8_t match = 1u;
            for (spin = 0u; spin < 200u; spin++) {   /* DMA settle         */
            }
            for (i = 0u; i < 6u; i++) {
                if (conn_rx[9u + i] != addr[i]) {     /* AdvA at rx[9..14]  */
                    match = 0u;
                    break;
                }
            }
            if (match) {
                /* LLData at conn_rx[15..36] (payload starts at [3]:
                 * InitA[6] AdvA[6] then LLData). */
                sh->conn_aa = (uint32_t)conn_rx[15] |
                              ((uint32_t)conn_rx[16] << 8) |
                              ((uint32_t)conn_rx[17] << 16) |
                              ((uint32_t)conn_rx[18] << 24);
                sh->conn_crcinit = (uint32_t)conn_rx[19] |
                                   ((uint32_t)conn_rx[20] << 8) |
                                   ((uint32_t)conn_rx[21] << 16);
                sh->conn_winsize = conn_rx[22];
                sh->conn_winoffset = (uint16_t)(conn_rx[23] |
                                                ((uint16_t)conn_rx[24] << 8));
                sh->conn_interval = (uint16_t)(conn_rx[25] |
                                               ((uint16_t)conn_rx[26] << 8));
                sh->conn_timeout = (uint16_t)(conn_rx[29] |
                                              ((uint16_t)conn_rx[30] << 8));
                for (i = 0u; i < 5u; i++) {
                    sh->conn_chm[i] = conn_rx[31u + i];
                }
                sh->conn_hop = (uint8_t)(conn_rx[36] & 0x1Fu);
                connected = 1u;
            } else {
                r->TASKS_DISABLE = 1u;
                for (spin = 0u; spin < 8000u; spin++) {
                    if (r->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
            }
        } else {
            r->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        }
        if (!connected) {
            chan = (uint8_t)((chan + 1u) % 3u);
        }
        if (sh->cmd != 0u) {                     /* honour STOP / new cmd    */
            break;
        }
    }
    sh->conn_state = connected ? 1u : 2u;        /* 1 connected, 2 gave up   */
}

/* Echo service: consume an app->flpr message, mirror it back, ring. */
static void flpr_echo_pump(tiku_flpr_shared_t *sh, uint32_t *last_seq)
{
    uint32_t seq = sh->a2f_seq;
    uint32_t len, i;

    if (seq == *last_seq) {
        return;
    }
    *last_seq = seq;
    len = sh->a2f_len;
    if (len > TIKU_FLPR_MSG_CAP) {
        len = TIKU_FLPR_MSG_CAP;
    }
    for (i = 0u; i < len; i++) {
        sh->f2a_buf[i] = sh->a2f_buf[i];
    }
    sh->f2a_len = len;
    sh->f2a_seq = sh->f2a_seq + 1u;
    flpr_doorbell_to_app();
}

void tiku_flpr_main(void)
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    volatile uint32_t pace;
    uint32_t last_seq = 0u;

    /* Enable the VPR's RT-peripheral interface (keyed VPRNORDICCTRL CSR,
     * 0x7C0: NORDICKEY=0x507D<<16 | ENABLERTPERIPH).  Until this runs, the
     * whole VEVIF register half of VPR00 (tasks/events/INTEN, offsets
     * <0x800) is dead from BOTH sides: bus reads return zero and writes
     * are ignored -- which looked exactly like \"INTENSET does not stick\"
     * from the app core.  Host-side control (INITPC/CPURUN, >=0x800) works
     * regardless. */
    __asm__ volatile ("csrw 0x7C0, %0" :: "r"((0x507Du << 16) | 1u));

    /* Un-inhibit the machine counters (mcountinhibit, 0x320): mcycle is
     * the pulse engine's timebase and RISC-V cores may reset with the
     * counters gated -- a constant mcycle turns the pacing wait into an
     * infinite loop. */
    __asm__ volatile ("csrw 0x320, zero");

    sh->heartbeat = 0u;
    sh->magic = TIKU_FLPR_MAGIC;

    for (;;) {
        /* Cooperative park/resume (see tiku_flpr_ipc.h): heartbeat freezes
         * while parked; RESUME returns to this loop.  The parked wait is a
         * paced busy-poll for now -- a WFI here could never wake, since no
         * interrupt source is wired to the VPR yet (the F2 doorbell will
         * turn parking into genuine sleep). */
        if (sh->cmd == TIKU_FLPR_CMD_PARK) {
            sh->rsp = TIKU_FLPR_RSP_PARKED;
            while (sh->cmd != TIKU_FLPR_CMD_RESUME) {
                for (pace = 0u; pace < 8000u; pace++) {
                }
            }
            sh->cmd = 0u;
            sh->rsp = 0u;
        }
        if (sh->cmd == TIKU_FLPR_CMD_PULSE) {
            tiku_flpr_pulse_t p;
            p.half_cycles = ((const volatile tiku_flpr_pulse_t *)
                             sh->a2f_buf)->half_cycles;
            p.edges = ((const volatile tiku_flpr_pulse_t *)
                       sh->a2f_buf)->edges;
            sh->cmd = 0u;
            flpr_pulse(&p);                    /* blocking, bounded        */
            sh->rsp = TIKU_FLPR_RSP_PULSE_DONE;
        }
        if (sh->cmd == TIKU_FLPR_CMD_BEACON) {
            const volatile tiku_flpr_beacon_t *b =
                (const volatile tiku_flpr_beacon_t *)sh->a2f_buf;
            uint32_t i, n = b->pdu_len;
            if (n > sizeof(beacon_pdu)) {
                n = sizeof(beacon_pdu);
            }
            for (i = 0u; i < n; i++) {
                beacon_pdu[i] = b->pdu[i];
            }
            /* interval_ms -> pace iterations: 128000 cycles/ms / DIV. */
            beacon_pace_iters = b->interval_ms * (128000u / FLPR_PACE_DIV);
            sh->beacon_bursts = 0u;
            beacon_on = 1u;
            sh->cmd = 0u;
        }
        if (sh->cmd == TIKU_FLPR_CMD_BEACON_STOP) {
            beacon_on = 0u;
            sh->cmd = 0u;
            sh->rsp = TIKU_FLPR_RSP_BEACON_STOPPED;
        }
        if (sh->cmd == TIKU_FLPR_CMD_RXPROBE) {
            sh->rx_done = 0u;
            sh->cmd = 0u;
            flpr_rxprobe(sh);                    /* blocking, bounded        */
        }
        if (sh->cmd == TIKU_FLPR_CMD_CONN_ADV) {
            sh->cmd = 0u;
            flpr_conn_adv(sh);                   /* blocking, bounded        */
        }
        if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
            sh->conn_state = 2u;
            sh->cmd = 0u;
        }
        if (beacon_on) {
            volatile uint32_t w;
            flpr_beacon_burst();
            sh->beacon_bursts = sh->beacon_bursts + 1u;
            sh->heartbeat = sh->heartbeat + 1u;
            /* Interval pacing; broken into slices so STOP/PARK commands
             * are honoured within ~a millisecond. */
            for (w = 0u; w < beacon_pace_iters; w += 12800u) {
                volatile uint32_t s;
                for (s = 0u; s < 12800u; s++) {
                }
                if (sh->cmd != 0u) {
                    break;
                }
            }
            continue;
        }
        flpr_echo_pump(sh, &last_seq);
        sh->heartbeat = sh->heartbeat + 1u;
        for (pace = 0u; pace < 8000u; pace++) {
        }
    }
}
