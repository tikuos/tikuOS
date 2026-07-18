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

/* --- Link-layer helpers ported from tiku_radio_arch.c (pure logic) --- */

/* BLE data-channel index -> RADIO FREQUENCY register value. */
static uint8_t flpr_data_freq(uint8_t k)
{
    return (k <= 10u) ? (uint8_t)(4u + 2u * k)
                      : (uint8_t)(28u + 2u * (k - 11u));
}

/* CSA#1 next data channel (unmapped hop + channel-map remap). */
static uint8_t flpr_csa1_next(uint8_t *last_unmapped, uint8_t hop,
                              const uint8_t chmap[5])
{
    uint8_t un = (uint8_t)((*last_unmapped + hop) % 37u);
    uint8_t n = 0u, idx, c;

    *last_unmapped = un;
    if (chmap[un >> 3] & (uint8_t)(1u << (un & 7u))) {
        return un;
    }
    for (c = 0u; c < 37u; c++) {
        if (chmap[c >> 3] & (uint8_t)(1u << (c & 7u))) {
            n++;
        }
    }
    idx = (uint8_t)(un % n);
    for (c = 0u; c < 37u; c++) {
        if (chmap[c >> 3] & (uint8_t)(1u << (c & 7u))) {
            if (idx == 0u) {
                return c;
            }
            idx--;
        }
    }
    return 0u;
}

/* SN/NESN: advance sn on ack, nesn on genuinely-new PAYLOAD-bearing data
 * (the has_payload gate = the L5 reliability fix). */
static uint8_t flpr_ll_ack(uint8_t *sn, uint8_t *nesn, uint8_t rx_sn,
                           uint8_t rx_nesn, uint8_t has_payload)
{
    uint8_t r = 0u;
    if ((rx_nesn & 1u) != *sn) {
        *sn ^= 1u;
        r |= 2u;                                /* ACKED                    */
    }
    if (has_payload && (rx_sn & 1u) == *nesn) {
        *nesn ^= 1u;
        r |= 1u;                                /* NEWDATA                  */
    }
    return r;
}

/* --- LL control + L2CAP forwarding on the FLPR (Phase B).  The FLPR is a
 * pure CONTROLLER: one pending PDU (LL control OR an L2CAP frame from the
 * host), retransmitted via SN/NESN until acked.  A received L2CAP data PDU
 * is forwarded verbatim to the M33 host over f2a; the host's response frame
 * comes back over a2f and is queued as an LLID=2 data PDU.  ATT/GATT lives
 * on the M33 now (tiku_ble_host) -- the coprocessor no longer parses it. */
static uint8_t  fll_tx[40];             /* pending PDU [hdr][len][S1][pay]  */
static uint8_t  fll_tx_len;             /* payload len; 0 = none            */
static uint8_t  fll_tx_llid;            /* 2 L2CAP / 3 control              */
static uint8_t  fll_sent_vers;          /* we queued our VERSION_IND        */
static uint8_t  fll_sn, fll_nesn;       /* link-layer sequence bits         */
static uint32_t fll_a2f_seen;           /* last a2f_seq consumed (TX frame) */

/* Phase A -- LL_CHANNEL_MAP_UPDATE_IND (0x01) / LL_CONNECTION_UPDATE_IND
 * (0x00) are INDICATIONS: the central commits and switches at the Instant
 * whether or not we ack them, so declining with LL_UNKNOWN_RSP (the old
 * fall-through) left us on stale params -> desync -> dropped link (audit
 * G6, and phones renegotiate the interval within the first second).  We
 * parse the Instant and apply at that connection event.  The follow-the-
 * central hold needs no precise timebase for this: swap the channel map
 * before the event's CSA#1 pick, and for a connection update just drop the
 * anchor so the wide-window re-acquire re-locks to the central's new
 * interval + transmit-window offset.  An off-by-one in the event count
 * costs at most one extra miss, then both sides re-sync. */
static uint8_t  fll_cm_pending;         /* channel-map update armed          */
static uint16_t fll_cm_instant;         /* connEventCount to apply it at     */
static uint8_t  fll_cm_map[5];
static uint8_t  fll_cu_pending;         /* connection update armed           */
static uint16_t fll_cu_instant;
static uint16_t fll_cu_interval;        /* new interval, 1.25 ms (telemetry) */
static uint16_t fll_cu_timeout;         /* new supervision, 10 ms (telemetry)*/

static void fll_queue_raw(uint8_t llid, const uint8_t *p, uint8_t plen)
{
    uint8_t i;
    if (fll_tx_len != 0u || plen == 0u) {
        return;                         /* one PDU in flight                */
    }
    fll_tx_llid = llid;
    fll_tx[1] = plen;
    fll_tx[2] = p[0];                   /* S1 = payload[0] (erratum-49)     */
    for (i = 0u; i < plen; i++) {
        fll_tx[3u + i] = p[i];
    }
    fll_tx_len = plen;
}

static void fll_queue_ctrl(uint8_t op, const uint8_t *d, uint8_t dl)
{
    uint8_t p[16], i;
    p[0] = op;
    for (i = 0u; i < dl; i++) {
        p[1u + i] = d[i];
    }
    fll_queue_raw(3u, p, (uint8_t)(1u + dl));
}

/* Build the outgoing PDU (pending, else empty) with current SN/NESN. */
static uint8_t fll_build_tx(uint8_t *out)
{
    uint8_t i;
    if (fll_tx_len != 0u) {
        for (i = 0u; i < 3u + fll_tx_len; i++) {
            out[i] = fll_tx[i];
        }
        out[0] = (uint8_t)((fll_tx_llid & 0x03u) |
                           (fll_nesn << 2) | (fll_sn << 3));
        out[2] = out[3];
        return (uint8_t)(3u + fll_tx_len);
    }
    out[0] = (uint8_t)(0x01u | (fll_nesn << 2) | (fll_sn << 3));
    out[1] = 0u;
    out[2] = out[0];
    return 3u;
}

/* Dispatch a genuinely-new RX PDU: LL control (handled here) or an L2CAP
 * frame (forwarded verbatim to the M33 host over f2a -- Phase B). */
static void fll_handle_rx(const uint8_t *buf, tiku_flpr_shared_t *sh)
{
    uint8_t llid = buf[0] & 0x03u, op;
    if (buf[1] == 0u) {
        return;
    }
    if (llid == 0x02u) {                        /* L2CAP frame -> host       */
        uint8_t n = buf[1], i;                  /* [len][CID][payload]       */
        if (n > TIKU_FLPR_MSG_CAP) {
            n = TIKU_FLPR_MSG_CAP;
        }
        for (i = 0u; i < n; i++) {
            sh->f2a_buf[i] = buf[3u + i];       /* payload starts at buf[3]  */
        }
        sh->f2a_len = n;
        sh->f2a_seq = sh->f2a_seq + 1u;         /* hand off to the M33       */
        flpr_doorbell_to_app();
        return;
    }
    if (llid != 0x03u) {
        return;
    }
    op = buf[3];
    if (op == 0x0Cu) {                          /* VERSION_IND -> reply     */
        if (!fll_sent_vers) {
            static const uint8_t v[5] = { 0x0Cu, 0x59u, 0x00u, 0x01u, 0x00u };
            fll_queue_ctrl(0x0Cu, v, 5u);
            fll_sent_vers = 1u;
        }
    } else if (op == 0x12u) {                   /* LL_PING_REQ -> RSP       */
        fll_queue_ctrl(0x13u, (const uint8_t *)0, 0u);
    } else if (op == 0x08u) {                   /* FEATURE_REQ -> RSP (none)*/
        static const uint8_t none[8] = { 0u };
        fll_queue_ctrl(0x09u, none, 8u);
    } else if (op == 0x01u) {                   /* LL_CHANNEL_MAP_UPDATE_IND*/
        /* CtrData (payload[1..]): ChM[5], Instant[2].  payload[n]=buf[3+n].
         * No control response -- an IND is applied at its Instant, not
         * answered; the LL ack (NESN) already confirms receipt. */
        if (buf[1] >= 8u) {
            uint8_t i;
            for (i = 0u; i < 5u; i++) {
                fll_cm_map[i] = buf[4u + i];
            }
            fll_cm_instant = (uint16_t)(buf[9] | ((uint16_t)buf[10] << 8));
            fll_cm_pending = 1u;
        }
    } else if (op == 0x00u) {                   /* LL_CONNECTION_UPDATE_IND */
        /* CtrData: WinSize, WinOffset[2], Interval[2], Latency[2],
         * Timeout[2], Instant[2].  Interval/Timeout are telemetry; the
         * re-lock at the Instant absorbs WinOffset/WinSize. */
        if (buf[1] >= 12u) {
            fll_cu_interval = (uint16_t)(buf[7] | ((uint16_t)buf[8] << 8));
            fll_cu_timeout  = (uint16_t)(buf[11] | ((uint16_t)buf[12] << 8));
            fll_cu_instant  = (uint16_t)(buf[13] | ((uint16_t)buf[14] << 8));
            fll_cu_pending  = 1u;
        }
    } else if (op != 0x09u && op != 0x13u && op != 0x07u) {
        fll_queue_ctrl(0x07u, &op, 1u);         /* LL_UNKNOWN_RSP           */
    }
}

/* Hold the link (L6 F-L6.1 step 1b): continuous-RX connection events.  No
 * timebase needed -- the central paces at connInterval, so we open RX on
 * the CSA#1 channel and wait for its packet, hardware-T_IFS respond with
 * an empty PDU carrying our SN/NESN, and re-arm.  Channels advance per
 * event in lockstep with the central.  Supervision = a run of misses. */
static uint8_t conn_txb[40]    __attribute__((aligned(4)));
static uint8_t conn_datrx[48]  __attribute__((aligned(4)));

/* Anchored-RX: cut RADIO-on time without breaking the channel lock.  The
 * continuous-RX loop stays synced for free -- one catch == one CSA#1 hop ==
 * one connection event -- so we hold the RADIO OFF for the dead part of
 * each interval, then fall into that same catch.  The hard part is the
 * idle length: the FLPR's execution rate is CONTENDED by the (awake) M33
 * on the shared bus and is neither known nor stable, so any open-loop idle
 * (mcycle, or the beacon's M33-ASLEEP busy-loop calibration) overshoots by
 * multiple intervals -> multi-hop channel desync the re-acquire can't
 * close.  Instead this is CLOSED-LOOP: grow the idle in small steps using
 * the measured RX-wait (spin-to-ADDRESS) as feedback.  It self-calibrates
 * to whatever the real rate is, and because every step is tiny, any
 * overshoot is at most one step (<< one interval) and the wide re-acquire
 * recovers it.  The RX window tracks the measured wait + slack.  Units are
 * RX-loop iterations throughout, so idle and wait share a feedback scale. */
#define ARX_STEP       4000u     /* idle growth/measure step                */
#define ARX_RX_HI     24000u     /* wait longer than this => grow the idle  */
#define ARX_RX_LO     12000u     /* wait shorter than this => shrink it     */
#define ARX_SLACK     35000u     /* RX window margin over the measured wait */
#define ARX_WIN_MAX  750000u     /* re-acquire / cap window                 */
#define ARX_WIN_MIN   55000u     /* smallest tracked window                 */

static void flpr_conn_hold(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t aa = sh->conn_aa, crcinit = sh->conn_crcinit;
    uint8_t  hop = sh->conn_hop, chmap[5];
    uint8_t  last_un = 0u, k, i;
    uint32_t event = 0u, miss_run = 0u, spin;
    uint32_t idle_iters = 0u, win = ARX_WIN_MAX, rxon = 0u;
    uint8_t  have_anchor = 0u;
    uint16_t cec = 0u;                           /* connEventCount (wraps)    */

    for (i = 0u; i < 5u; i++) {
        chmap[i] = sh->conn_chm[i];
    }
    /* Reset the LL / L2CAP-transport state for this connection. */
    fll_tx_len = 0u; fll_tx_llid = 0u; fll_sent_vers = 0u;
    fll_sn = 0u; fll_nesn = 0u;
    fll_a2f_seen = sh->a2f_seq;
    sh->a2f_ack  = sh->a2f_seq;                  /* Phase B: TX slot free      */
    fll_cm_pending = 0u; fll_cu_pending = 0u;    /* Phase A: no update armed  */
    sh->conn_sub = 0u;
    sh->conn_gap = 0u; sh->conn_rxon = 0u;       /* telemetry: not yet anchored*/

    r->BASE0   = aa << 8;                        /* BALEN=3: base in top 3 B */
    r->PREFIX0 = (aa >> 24) & 0xFFu;
    r->CRCINIT = crcinit & 0x00FFFFFFu;

    for (;;) {
        uint8_t got = 0u, txn;

        /* Phase A: apply a pending update when cec reaches its Instant (the
         * wrap-safe >= test also fires if a miss-burst skipped the exact
         * value).  The map swap must precede this event's CSA#1 pick so we
         * land on the same channel the central now uses; the connection
         * update drops the anchor so the wide re-acquire re-locks to the
         * central's new interval + WinOffset. */
        if (fll_cm_pending &&
            (uint16_t)(cec - fll_cm_instant) < 0x8000u) {
            for (i = 0u; i < 5u; i++) {
                chmap[i] = fll_cm_map[i];
                sh->conn_chm[i] = fll_cm_map[i];
            }
            fll_cm_pending = 0u;
            sh->conn_cm = sh->conn_cm + 1u;
        }
        if (fll_cu_pending &&
            (uint16_t)(cec - fll_cu_instant) < 0x8000u) {
            sh->conn_interval = fll_cu_interval;
            sh->conn_timeout  = fll_cu_timeout;
            have_anchor = 0u;                    /* drop lock -> re-acquire   */
            idle_iters  = 0u;
            win = ARX_WIN_MAX;
            fll_cu_pending = 0u;
            sh->conn_cu = sh->conn_cu + 1u;
        }

        k = flpr_csa1_next(&last_un, hop, chmap);
        r->FREQUENCY = flpr_data_freq(k);
        r->DATAWHITE = 0x00890000u | (0x40u | k);

        /* Anchored idle: hold the RADIO OFF for the (closed-loop) idle
         * count, using the RX-loop body (an EVENTS_DISABLED read that stays
         * 0 while idle) so idle and the RX-wait below share a feedback
         * scale.  STOP is polled coarsely so it lands within the idle. */
        if (have_anchor && idle_iters != 0u) {
            r->EVENTS_DISABLED = 0u;
            for (spin = 0u; spin < idle_iters; spin++) {
                if (r->EVENTS_DISABLED != 0u) {  /* never set while idle      */
                    break;
                }
                if ((spin & 0x3FFFu) == 0u &&
                    sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
                    break;
                }
            }
        }
        if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
            break;
        }

        /* RX with the hardware T_IFS turnaround to TX (DISABLED_TXEN). */
        r->SHORTS = (1u << 0) | (1u << 19) | (1u << 2) | (1u << 4);
        r->PACKETPTR = (uint32_t)conn_datrx;
        r->EVENTS_ADDRESS  = 0u;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_CRCOK    = 0u;
        r->EVENTS_CRCERROR = 0u;
        (void)r->EVENTS_DISABLED;
        r->TASKS_RXEN = 1u;

        /* Window tracks the last measured wait + slack when locked; wide on
         * re-acquire (anchor position unknown). */
        for (spin = 0u; spin < win; spin++) {
            if (r->EVENTS_ADDRESS != 0u) {
                got = 1u;
                break;
            }
        }
        if (!got) {                              /* no packet: cancel + rot  */
            r->SHORTS = (1u << 0) | (1u << 19);
            r->TASKS_DISABLE = 1u;
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            /* Lost lock: back the idle WAY off (the overshoot that caused
             * this) and re-acquire on the wide window.  Keeping a fraction
             * of the idle means we don't restart the climb from zero. */
            have_anchor = 0u;
            idle_iters  = idle_iters / 2u;
            win         = ARX_WIN_MAX;
            /* Supervision: each re-acquire miss is a wide (~ARX_WIN_MAX) RX
             * window ~200 ms at the contended FLPR rate, so ~30 CONSECUTIVE
             * misses ~= 6 s of silence -> declare the link dead.  A healthy
             * link resets miss_run on every catch, so 30-in-a-row means the
             * peer is genuinely gone (BLE supervision is ~4 s). */
            if (++miss_run > 30u) {
                break;
            }
            if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
                break;
            }
            cec++;                               /* a miss is still one event */
            continue;                            /* miss: no serviced event  */
        }
        miss_run = 0u;
        rxon = spin;                             /* RX-wait iters this event  */
        /* Closed-loop: creep the idle toward the point where the wait sits
         * in [RX_LO, RX_HI].  Small steps up (grow while the packet lands
         * late), a bigger step down (safety) when it lands early.  The
         * window then just covers the measured wait plus slack. */
        if (rxon > ARX_RX_HI) {
            idle_iters += ARX_STEP;
        } else if (rxon < ARX_RX_LO && idle_iters > 4u * ARX_STEP) {
            idle_iters -= 4u * ARX_STEP;
        }
        win = rxon + ARX_SLACK;
        if (win > ARX_WIN_MAX) {
            win = ARX_WIN_MAX;
        } else if (win < ARX_WIN_MIN) {
            win = ARX_WIN_MIN;
        }
        /* CRC verdict lands just after PHYEND; bounded so we stay inside
         * the 150 us T_IFS window before the auto-TX reads conn_txb. */
        for (spin = 0u; spin < 3000u; spin++) {
            if (r->EVENTS_CRCOK != 0u || r->EVENTS_CRCERROR != 0u) {
                break;
            }
        }
        if (r->EVENTS_CRCOK != 0u) {
            uint8_t h = conn_datrx[0];
            uint8_t rc = flpr_ll_ack(&fll_sn, &fll_nesn,
                                     (uint8_t)((h >> 3) & 1u),
                                     (uint8_t)((h >> 2) & 1u),
                                     (uint8_t)(conn_datrx[1] != 0u));
            if (rc & 2u) {                       /* our last PDU landed      */
                fll_tx_len = 0u;
            }
            if (rc & 1u) {                       /* genuinely-new payload    */
                fll_handle_rx(conn_datrx, sh);
            }
        }
        /* Build our response (pending LL/ATT PDU, else empty) with the
         * updated SN/NESN.  Drop DISABLED_TXEN so the response's own
         * DISABLED cannot re-trigger a spurious TX (the step-0 fix). */
        txn = fll_build_tx(conn_txb);
        (void)txn;
        r->SHORTS = (1u << 0) | (1u << 19);
        r->PACKETPTR = (uint32_t)conn_txb;
        r->EVENTS_PHYEND   = 0u;
        r->EVENTS_DISABLED = 0u;
        for (spin = 0u; spin < 40000u; spin++) { /* T_IFS TX completes       */
            if (r->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        /* TX: a new L2CAP frame from the host (a2f) -> queue as an LLID=2
         * data PDU when the pending slot is free.  The host has already
         * built the full frame ([len][CID][ATT]); the controller just moves
         * it on-air.  a2f_ack tells the host the slot is consumed. */
        if (fll_tx_len == 0u && sh->a2f_seq != fll_a2f_seen) {
            uint8_t n = (uint8_t)sh->a2f_len, fr[32];
            fll_a2f_seen = sh->a2f_seq;
            if (n > sizeof(fr)) {
                n = (uint8_t)sizeof(fr);
            }
            for (i = 0u; i < n; i++) {
                fr[i] = sh->a2f_buf[i];
            }
            fll_queue_raw(2u, fr, n);
            sh->a2f_ack = sh->a2f_seq;            /* slot free for the host   */
        }
        have_anchor = 1u;                        /* locked: idle next event   */
        sh->conn_gap  = idle_iters;              /* telemetry: off iters      */
        sh->conn_rxon = rxon;                    /* telemetry: RX-wait iters  */
        sh->conn_events = ++event;
        cec++;                                   /* serviced event advances   */
        if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
            break;
        }
    }
    sh->conn_state = 3u;                         /* link ended               */
}

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
    sh->conn_cm = 0u;                            /* Phase A telemetry         */
    sh->conn_cu = 0u;
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
    if (connected) {
        sh->conn_state = 1u;                     /* connected -> M33 sees it */
        flpr_conn_hold(sh);                      /* then hold autonomously   */
    } else {
        sh->conn_state = 2u;                     /* gave up advertising      */
    }
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
