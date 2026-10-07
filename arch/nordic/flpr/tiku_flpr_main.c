/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_main.c - FLPR coprocessor firmware.
 *
 * Runs on the VPR RISC-V core out of the SRAM carve and serves the command
 * word in the shared page: mailbox echo, pulse engine, beacon offload, RX
 * probe, a BLE connection controller, a compute load and the fault park.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/flpr/tiku_flpr_ipc.h>
#include <arch/nordic/mdk/nrf54l15.h>   /* plain-C register structs: the
                                         * same MDK headers the M33 uses
                                         * compile unchanged for RISC-V.
                                         * This core is a non-secure bus
                                         * master, so all access is via the
                                         * _NS aliases, and reaches only
                                         * peripherals the app core made
                                         * non-secure first (SPU). */

/* Outward doorbell: bus writes to EVENTS_TRIGGERED[n] are ignored; the VPR
 * raises the events through its VEVIF CSR (VPRCSR_NORDIC_EVENTS = 0x7E2,
 * bit n = channel n).  Only channels 16..22 have INTEN bits toward the app
 * core, so the app doorbell is channel 16. */
#define FLPR_DOORBELL_CH   16u

/** @brief Raise doorbell channel 16 toward the app core. */
static inline void flpr_doorbell_to_app(void)
{
    /* The VEVIF EVENTS CSR (0x7E2).  A bus access to the VPR00 VEVIF
     * register space from this core bus-faults the VPR.  The app core does
     * not rely on the doorbell: it reads the mailbox sequence
     * (tiku_flpr_arch_poll(), tiku_flpr_arch_conn_recv()). */
    __asm__ volatile ("csrs 0x7E2, %0" :: "r"(1u << FLPR_DOORBELL_CH));
}

/* Pulse engine: 50%-duty waveform on the VIO pin.
 *
 * VIO CSRs give the VPR single-cycle pin access without the AHB GPIO block,
 * which a non-secure master cannot reach: DIR=0xBC1, OUT=0xBC0.
 *
 * Pacing is a busy loop: a wait on mcycle stalls on this core, even with
 * mcountinhibit cleared.  The loop is deterministic because this core
 * services no interrupts.  FLPR_PACE_DIV converts half-period cycles to loop
 * iterations, calibrated against the M33's clock (see /sys/flpr/pulse ms=). */
#define FLPR_PACE_DIV  10u

/** @brief Emit @p p->edges toggles on the VIO pin, then park it low. */
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

/* Beacon offload: one 3-channel BLE advertising burst per interval, paced by
 * the busy loop, while the app core sleeps.
 *
 * The M33 programs every link-config register (MODE, PCNF, CRC, access
 * address, TXPOWER, SHORTS, the erratum-49 S1 layout) while the RADIO is
 * still secure, holds CONSTLAT for the session (erratum 20), and makes RADIO
 * and UARTE21 non-secure so this core can reach them.  Per burst this
 * firmware starts the UARTE21 clock kick, then per channel writes FREQUENCY,
 * DATAWHITE and PACKETPTR, triggers TXEN and polls DISABLED.  The PDU lives
 * in this core's .bss (the non-secure carve), which the radio's EasyDMA can
 * then read. */
static const uint8_t beacon_freq[3] = { 2u, 26u, 80u };
static const uint8_t beacon_widx[3] = { 37u, 38u, 39u };
static uint8_t beacon_pdu[48] __attribute__((aligned(4)));
static uint8_t beacon_kick[16];
static uint32_t beacon_pace_iters;      /* interval in pace iterations     */
static uint32_t beacon_on;

/**
 * @brief Start a 16-byte UARTE21 transmit, which holds the HF clock on
 *        through a burst.
 */
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

/** @brief Transmit the beacon PDU once on each of channels 37, 38 and 39. */
static void flpr_beacon_burst(void)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t c, spin;

    flpr_hfclk_kick();
    /* Set the burst's own shorts and disable the radio first: an advertise
     * session leaves a turnaround to RX armed on DISABLED, under which the
     * first channel's end opens a receiver that never closes and the other
     * two channels never transmit. */
    r->SHORTS = (1u << 0) | (1u << 19);       /* READY_START, PHYEND_DISABLE */
    r->EVENTS_DISABLED = 0u;
    r->TASKS_DISABLE = 1u;
    for (spin = 0u; spin < 8000u; spin++) {
        if (r->EVENTS_DISABLED != 0u) {
            break;
        }
    }
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

/* RX probe target.  The M33 sets RXADDRESSES, BASE0 and the packet format
 * while the RADIO is secure; the probe writes only FREQUENCY, DATAWHITE,
 * PACKETPTR, SHORTS and the RX task. */
static uint8_t rxprobe_buf[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/**
 * @brief Listen on advertising channel 37 for 1500 short RX windows and
 *        report into the rx_* fields.
 *
 * Counts ADDRESS and CRCOK events, copies the head of the first CRC-valid
 * packet, and stops early when a new command arrives.
 */
static void flpr_rxprobe(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    uint32_t addr_evts = 0u, crcok_evts = 0u, attempt, spin;
    uint8_t  got_first = 0u;

    flpr_hfclk_kick();                          /* HFCLK up for RX          */
    r->FREQUENCY = 2u;                          /* ch37 = 2402 MHz          */
    r->DATAWHITE = 0x00890000u | (0x40u | 37u);
    /* Shorts: READY_START, ADDRESS_RSSISTART, RXREADY_START and
     * PHYEND_DISABLE.  Each window polls up to 40000 times. */
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
                /* A fixed 15-byte head after a short settle: at CRCOK
                 * EasyDMA can still be writing the packet, and LEN can read
                 * 0. */
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

/* Connection controller, advertise and capture: per channel, TX the
 * connectable ADV_IND (the DISABLED_RXEN short opens RX), then listen for a
 * CONNECT_IND addressed here and parse its LLData (data-channel access
 * address, CRC init, timing) into the conn_* fields.  The register sequence
 * follows the M33's advertising phase in tiku_radio_arch_connect; the M33
 * sets the packet format, advertising access address and CRC before the
 * security flip. */
static uint8_t conn_adv[48] __attribute__((aligned(4)));
/* The SCAN_RSP answering a SCAN_REQ: the ADV_IND payload under PDU type 4. */
static uint8_t conn_rsp[48] __attribute__((aligned(4)));
/* RX DMA target: it must hold [S0][LEN][S1] + PCNF1.MAXLEN
 * (TIKU_FLPR_DLE_MAX_OCTETS) bytes.  The DMA writes up to MAXLEN on any
 * address match, CRC-valid or not, so a smaller buffer is overrun into the
 * next .bss object. */
static uint8_t conn_rx[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/* --- Link-layer helpers (the same logic as tiku_radio_arch.c) --- */

/** @brief BLE data-channel index -> RADIO FREQUENCY register value. */
static uint8_t flpr_data_freq(uint8_t k)
{
    return (k <= 10u) ? (uint8_t)(4u + 2u * k)
                      : (uint8_t)(28u + 2u * (k - 11u));
}

/** @brief CSA#1 next data channel (unmapped hop + channel-map remap). */
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

/**
 * @brief Update SN/NESN from a received header.
 *
 * sn advances when the peer acks; nesn advances on every new packet, an
 * empty one included (Core Vol 6 Part B 4.5.9), or the peer resends it.
 *
 * @return Bit 1 (ACKED) and bit 0 (NEWDATA: a new packet with a payload).
 */
static uint8_t flpr_ll_ack(uint8_t *sn, uint8_t *nesn, uint8_t rx_sn,
                           uint8_t rx_nesn, uint8_t has_payload,
                           uint8_t room)
{
    uint8_t r = 0u;
    if ((rx_nesn & 1u) != *sn) {
        *sn ^= 1u;
        r |= 2u;                                /* ACKED                    */
    }
    if ((rx_sn & 1u) == *nesn && room) {        /* no room: NESN stays, so  */
                                                /* the peer sends it again  */
        *nesn ^= 1u;
        if (has_payload) {
            r |= 1u;                            /* NEWDATA                  */
        }
    }
    return r;
}

/* --- LL control and L2CAP forwarding.  The FLPR is the controller only:
 * one pending PDU (LL control or an L2CAP fragment from the host),
 * retransmitted through SN/NESN until acked.  A received L2CAP data PDU is
 * forwarded unchanged to the M33 host over f2a; the host's fragments come
 * back over a2f and are queued as data PDUs.  ATT/GATT runs on the M33
 * (tiku_ble_host). */
static uint8_t  fll_tx[TIKU_FLPR_DLE_BUF_SIZE]; /* [hdr][len][S1][pay]     */
static uint8_t  fll_tx_len;             /* payload len; 0 = none            */
static uint8_t  fll_tx_sent;            /* the last packet sent carried it  */
static uint8_t  fll_tx_llid;            /* 2 L2CAP / 3 control              */
static uint8_t  fll_sent_vers;          /* VERSION_IND has been queued      */
static uint8_t  fll_sn, fll_nesn;       /* link-layer sequence bits         */
static uint32_t fll_a2f_seen;           /* last a2f_seq consumed (TX frame) */

/* LL_CHANNEL_MAP_UPDATE_IND (0x01) and LL_CONNECTION_UPDATE_IND (0x00) are
 * indications: the central switches at the Instant whether or not they are
 * acked, and a peripheral still on the old parameters loses the link.  The
 * controller records the Instant and applies the update at that event: the
 * channel map is swapped before the event's CSA#1 pick, and a connection
 * update drops the anchor so the wide re-acquire window locks to the new
 * interval and window offset.  An off-by-one in the event count costs one
 * extra miss. */
static uint8_t  fll_cm_pending;         /* channel-map update armed          */
static uint16_t fll_cm_instant;         /* connEventCount to apply it at     */
static uint8_t  fll_cm_map[5];
static uint8_t  fll_cu_pending;         /* connection update armed           */
static uint16_t fll_cu_instant;
static uint16_t fll_cu_interval;        /* new interval, 1.25 ms             */
static uint16_t fll_cu_timeout;         /* new supervision, 10 ms            */
static uint16_t fll_cu_winoffset;       /* its transmit window, 1.25 ms      */
static uint8_t  fll_cu_winsize;
static uint8_t  fll_terminate;          /* LL_TERMINATE_IND: end after reply */
static uint8_t  fll_reason;             /* why the link ends, an HCI reason  */
static uint8_t  fll_term_sent;          /* the host's queued: end when acked */
/* PHY update: the new PHY (RADIO MODE, PCNF0) applies at its Instant. */
static uint8_t  fll_phy_pending;       /* LL_PHY_UPDATE_IND armed            */
static uint8_t  fll_phy_new;           /* target: 0 1M, 1 2M, 2 Coded S8     */
static uint16_t fll_phy_instant;       /* connEventCount to switch at        */

/** @brief Queue one PDU with @p llid unless a PDU is already pending. */
/* LL encryption, the peripheral's side.  The central's LL_ENC_REQ goes to
 * the M33, which owns the TRNG and the host's LTK: it returns SKDs and IVs
 * for the LL_ENC_RSP, then the session key or a refusal.  CCM00 encrypts a
 * PDU once, as it is queued (a resend keeps its ciphertext: the MIC leaves
 * NESN and SN out), and decrypts a new one once the reply is out. */
#define FLL_ENC_OFF     0u      /* plaintext                             */
#define FLL_ENC_SKD     1u      /* LL_ENC_REQ posted: SKDs and IVs due   */
#define FLL_ENC_KEY     2u      /* LL_ENC_RSP queued: the key is due     */
#define FLL_ENC_START   3u      /* LL_START_ENC_REQ queued: RX decrypts  */
#define FLL_ENC_RSP     4u      /* the central's LL_START_ENC_RSP heard  */
#define FLL_ENC_DONE    5u      /* the reply queued, encrypted           */
#define FLL_ENC_ON      6u      /* both ways, acknowledged               */
static uint8_t  fll_enc;                /* FLL_ENC_*                        */
static uint8_t  fll_rx_enc, fll_tx_enc; /* decrypting RX, encrypting TX     */
static uint32_t fll_rx_ctr, fll_tx_ctr; /* packetCounter each way           */
static uint32_t fll_enc_seq;            /* the enc_req_seq posted           */
static uint8_t  fll_sk[16];             /* session key, MSB first           */
static uint8_t  fll_iv[8];              /* IVm || IVs, as sent              */

/* CCM00's job lists and their typed fields, one crypt at a time. */
static uint32_t ccm_in[10]  __attribute__((aligned(4)));
static uint32_t ccm_out[10] __attribute__((aligned(4)));
static uint16_t ccm_alen_i  __attribute__((aligned(4)));
static uint16_t ccm_mlen_i  __attribute__((aligned(4)));
static uint16_t ccm_alen_o  __attribute__((aligned(4)));
static uint16_t ccm_mlen_o  __attribute__((aligned(4)));
static uint8_t  ccm_aad_i   __attribute__((aligned(4)));
static uint8_t  ccm_aad_o   __attribute__((aligned(4)));
static uint8_t  ccm_buf[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/** @brief Load @p n bytes of @p src, reversed and zero-padded, into the
 *         four words of a CCM00 key or nonce register. */
static void ccm_load(volatile uint32_t *val, const uint8_t *src, uint8_t n)
{
    uint8_t img[16], i;
    for (i = 0u; i < 16u; i++) {
        img[i] = (i < n) ? src[n - 1u - i] : 0u;
    }
    for (i = 0u; i < 4u; i++) {
        val[i] = (uint32_t)img[4u * i] | ((uint32_t)img[4u * i + 1u] << 8) |
                 ((uint32_t)img[4u * i + 2u] << 16) |
                 ((uint32_t)img[4u * i + 3u] << 24);
    }
}

/**
 * @brief One CCM00 crypt of an LL payload, in place at @p p: @p len bytes
 *        to ciphertext and MIC, or @p len bytes and their MIC back to text,
 *        under the session key and the nonce of @p ctr and @p dir.
 * @return 1, or 0 for a MIC that fails or an engine that does not finish
 */
static uint8_t flpr_ccm(uint8_t decrypt, uint8_t dir, uint32_t ctr,
                        uint8_t aad, uint8_t *p, uint8_t len)
{
    NRF_CCM_Type *c = NRF_CCM00_NS;
    uint8_t  nonce[13], i, ok, olen;
    uint32_t spin;

    nonce[0] = (uint8_t)ctr;
    nonce[1] = (uint8_t)(ctr >> 8);
    nonce[2] = (uint8_t)(ctr >> 16);
    nonce[3] = (uint8_t)(ctr >> 24);
    nonce[4] = (uint8_t)(dir ? 0x80u : 0x00u);  /* 1: central to us */
    for (i = 0u; i < 8u; i++) {
        nonce[5u + i] = fll_iv[i];
    }
    c->ENABLE = 2u;
    c->MODE = (decrypt ? 2u : 0u) | (3u << 16) | (1u << 24); /* BLE, 1M, M4 */
    c->ADATAMASK = 0xE3u;                       /* NESN, SN, MD masked */
    ccm_load(c->KEY.VALUE, fll_sk, 16u);
    ccm_load(c->NONCE.VALUE, nonce, 13u);
    ccm_alen_i = 1u;
    ccm_aad_i = aad;
    ccm_mlen_i = (uint16_t)(decrypt ? len + 4u : len);
    olen = (uint8_t)(decrypt ? len : len + 4u);
    ccm_in[0] = (uint32_t)&ccm_alen_i;   ccm_in[1] = 2u | (11u << 24);
    ccm_in[2] = (uint32_t)&ccm_mlen_i;   ccm_in[3] = 2u | (12u << 24);
    ccm_in[4] = (uint32_t)&ccm_aad_i;    ccm_in[5] = 1u | (13u << 24);
    ccm_in[6] = (uint32_t)p;             ccm_in[7] = ccm_mlen_i | (14u << 24);
    ccm_in[8] = 0u;                      ccm_in[9] = 0u;
    ccm_out[0] = (uint32_t)&ccm_alen_o;  ccm_out[1] = 2u | (11u << 24);
    ccm_out[2] = (uint32_t)&ccm_mlen_o;  ccm_out[3] = 2u | (12u << 24);
    ccm_out[4] = (uint32_t)&ccm_aad_o;   ccm_out[5] = 1u | (13u << 24);
    ccm_out[6] = (uint32_t)ccm_buf;      ccm_out[7] = olen | (14u << 24);
    ccm_out[8] = 0u;                     ccm_out[9] = 0u;
    c->IN.PTR = (uint32_t)ccm_in;
    c->OUT.PTR = (uint32_t)ccm_out;
    c->EVENTS_END = 0u;
    c->EVENTS_ERROR = 0u;
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    c->TASKS_START = 1u;
    for (spin = 0u; spin < 200000u; spin++) {
        if (c->EVENTS_END != 0u || c->EVENTS_ERROR != 0u) {
            break;
        }
    }
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    ok = (uint8_t)(c->EVENTS_END != 0u && c->EVENTS_ERROR == 0u &&
                   (!decrypt || (c->MACSTATUS & 1u) != 0u));
    if (c->EVENTS_END == 0u) {
        c->TASKS_STOP = 1u;
    }
    c->ENABLE = 0u;
    if (ok) {
        for (i = 0u; i < olen; i++) {
            p[i] = ccm_buf[i];
        }
    }
    return ok;
}

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
    if (fll_tx_enc) {                   /* the session's next count, MIC on */
        if (!flpr_ccm(0u, 0u, fll_tx_ctr, llid, &fll_tx[3], plen)) {
            return;                     /* the engine failed: nothing sent  */
        }
        plen = (uint8_t)(plen + 4u);
        fll_tx[1] = plen;
        fll_tx[2] = fll_tx[3];
        fll_tx_ctr++;
    }
    fll_tx_len = plen;
}

/** @brief Queue an LL control PDU: opcode @p op and @p dl bytes of data. */
static void fll_queue_ctrl(uint8_t op, const uint8_t *d, uint8_t dl)
{
    uint8_t p[16], i;
    p[0] = op;
    for (i = 0u; i < dl; i++) {
        p[1u + i] = d[i];
    }
    fll_queue_raw(3u, p, (uint8_t)(1u + dl));
}

/** @brief Build the outgoing PDU (pending, else empty) with current SN/NESN. */
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

/**
 * @brief Dispatch a new RX PDU: LL control is handled here, an L2CAP
 *        fragment goes unchanged to the M33 host over f2a.
 */
static void fll_handle_rx(const uint8_t *buf, tiku_flpr_shared_t *sh)
{
    uint8_t llid = buf[0] & 0x03u, op;
    if (buf[1] == 0u) {
        return;
    }
    if (llid == 0x02u || llid == 0x01u) {       /* L2CAP fragment -> host    */
        uint8_t n = buf[1], i;                  /* len>0 (empty PDU returned) */
        if (n > TIKU_FLPR_MSG_CAP) {
            n = TIKU_FLPR_MSG_CAP;
        }
        for (i = 0u; i < n; i++) {
            sh->f2a_buf[i] = buf[3u + i];       /* payload starts at buf[3]  */
        }
        sh->f2a_len = n;
        sh->f2a_llid = llid;                    /* 2 start / 1 continuation  */
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
    } else if (op == 0x02u) {                   /* LL_TERMINATE_IND         */
        fll_terminate = 1u;                     /* ack it, then end         */
        fll_reason = (buf[1] >= 2u) ? buf[4] : 0x13u;   /* its ErrorCode    */
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
         * Timeout[2], Instant[2]; the Instant opens the new transmit
         * window after the old interval's anchor. */
        if (buf[1] >= 12u) {
            fll_cu_winsize   = buf[4];
            fll_cu_winoffset = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
            fll_cu_interval = (uint16_t)(buf[7] | ((uint16_t)buf[8] << 8));
            fll_cu_timeout  = (uint16_t)(buf[11] | ((uint16_t)buf[12] << 8));
            fll_cu_instant  = (uint16_t)(buf[13] | ((uint16_t)buf[14] << 8));
            fll_cu_pending  = 1u;
        }
    } else if (op == 0x03u) {                   /* LL_ENC_REQ               */
        /* CtrData Rand[8] EDIV[2] SKDm[8] IVm[4] at buf[4..25], posted for
         * the M33; data waits until the procedure ends. */
        if (fll_enc == FLL_ENC_OFF && buf[1] >= 23u) {
            uint8_t i;
            for (i = 0u; i < 8u; i++) {
                sh->enc_rand[i] = buf[4u + i];
                sh->enc_skdm[i] = buf[14u + i];
            }
            sh->enc_ediv = (uint16_t)(buf[12] | ((uint16_t)buf[13] << 8));
            for (i = 0u; i < 4u; i++) {
                sh->enc_ivm[i] = buf[22u + i];
            }
            fll_enc_seq = sh->enc_req_seq + 1u;
            sh->enc_req_seq = fll_enc_seq;
            fll_enc = FLL_ENC_SKD;
            flpr_doorbell_to_app();
        }
    } else if (op == 0x06u) {                   /* LL_START_ENC_RSP          */
        if (fll_enc == FLL_ENC_START) {
            fll_enc = FLL_ENC_RSP;              /* the reply goes encrypted  */
        }
    } else if (op == 0x14u) {                   /* LL_LENGTH_REQ (DLE)      */
        /* Reply with the local maximum and publish the effective TX size,
         * min(peer MaxRxOctets, TIKU_FLPR_DLE_MAX_OCTETS) and at least 27,
         * for the M33 host's L2CAP fragmentation.  CtrData: MaxRxOctets[2]
         * MaxRxTime[2] MaxTxOctets[2] MaxTxTime[2] (LE) at buf[4..11]. */
        if (buf[1] >= 9u) {
            uint16_t peer_rx = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
            uint32_t eff = (peer_rx < TIKU_FLPR_DLE_MAX_OCTETS)
                         ? peer_rx : TIKU_FLPR_DLE_MAX_OCTETS;
            uint8_t rsp[8];
            rsp[0] = (uint8_t)TIKU_FLPR_DLE_MAX_OCTETS; rsp[1] = 0u;
            rsp[2] = (uint8_t)TIKU_FLPR_DLE_MAX_TIME;
            rsp[3] = (uint8_t)(TIKU_FLPR_DLE_MAX_TIME >> 8);
            rsp[4] = (uint8_t)TIKU_FLPR_DLE_MAX_OCTETS; rsp[5] = 0u;
            rsp[6] = (uint8_t)TIKU_FLPR_DLE_MAX_TIME;
            rsp[7] = (uint8_t)(TIKU_FLPR_DLE_MAX_TIME >> 8);
            fll_queue_ctrl(0x15u, rsp, 8u);     /* LL_LENGTH_RSP            */
            if (eff < 27u) { eff = 27u; }
            sh->dle_max = eff;                  /* publish to the M33 host  */
        }
    } else if (op == 0x16u) {                   /* LL_PHY_REQ (PHY update)  */
        static const uint8_t rsp[2] = { 0x07u, 0x07u };  /* 1M + 2M + Coded */
        fll_queue_ctrl(0x17u, rsp, 2u);         /* LL_PHY_RSP              */
    } else if (op == 0x18u) {                   /* LL_PHY_UPDATE_IND        */
        /* CtrData: PHY_C_TO_P[1] PHY_P_TO_C[1] Instant[2] at buf[4..7].
         * Symmetric switch: the RX (C->P) and TX (P->C) take the same PHY;
         * apply RADIO MODE/PCNF0 at the Instant.  0=1M 1=2M 2=Coded S8. */
        if (buf[1] >= 5u) {
            fll_phy_new = ((buf[4] & 0x04u) != 0u) ? 2u
                        : ((buf[4] & 0x02u) != 0u) ? 1u : 0u;
            fll_phy_instant = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
            fll_phy_pending = 1u;
        }
    } else if (op != 0x09u && op != 0x13u && op != 0x07u &&
               op != 0x15u && op != 0x17u) {
        fll_queue_ctrl(0x07u, &op, 1u);         /* LL_UNKNOWN_RSP           */
    }
}

/* Connection events for the held link, on TIMER10's time line: the
 * CONNECT_IND's PHYEND cleared the timer, so it counts 2 MHz ticks from the
 * request's end, the reference the spec times the first event from.  Each
 * event opens RX around its nominal anchor, widened by both clocks' drift
 * since the last packet caught, on the CSA#1 channel advanced once per
 * interval, caught or missed.  ADDRESS and PHYEND are captured over DPPI
 * (CC[4], CC[3]), and COMPARE[0] fires the reply's TXEN at the T_IFS the
 * scan responses use; CC[5] is a software read of now. */
static uint8_t conn_txb[TIKU_FLPR_DLE_BUF_SIZE]   __attribute__((aligned(4)));
static uint8_t conn_datrx[TIKU_FLPR_DLE_BUF_SIZE] __attribute__((aligned(4)));

/* DPPIC10 channels between the RADIO and TIMER10, for advertising's timed
 * scan response and the connection's timed replies. */
#define FLPR_DPPI_CH_PHYEND  3u
#define FLPR_DPPI_CH_ADDR    4u
#define FLPR_DPPI_CH_TXEN    5u

#define T10_PER_US         2u      /* TIMER10 ticks per microsecond         */
#define CONN_RX_EARLY_US  60u      /* RX ramp-up and polling ahead of a     */
                                   /* window                                */
#define CONN_RX_LATE_US   60u      /* RX stays open past the latest start   */
#define CONN_SCA_LOCAL    50u      /* this side's clock accuracy, in ppm    */
#define CONN_REPLY_TICKS  200u     /* PHYEND to TXEN: 150 us on air         */

/* The central's sleep-clock accuracy from the CONNECT_IND (SCA bits 7:5 of
 * the hop byte), the upper bound of its band in ppm. */
static uint16_t fll_sca_ppm;
static const uint16_t fll_sca_table[8] = {
    500u, 250u, 150u, 100u, 75u, 50u, 30u, 20u
};

/** @brief TIMER10 now, in ticks. */
static inline uint32_t t10_now(void)
{
    NRF_TIMER10_NS->TASKS_CAPTURE[5] = 1u;
    return NRF_TIMER10_NS->CC[5];
}

/** @brief Preamble plus access address on the current PHY, in us: the
 *         ADDRESS event's lag behind a packet's start. */
static uint32_t conn_aa_us(uint32_t phy)
{
    return (phy == 2u) ? 336u : (phy == 1u) ? 24u : 40u;
}

/** @brief Window widening after @p dt ticks without a packet: both clocks'
 *         drift over that span plus 32 us, in ticks. */
static uint32_t conn_widen(uint32_t dt)
{
    uint32_t ms = dt / (1000u * T10_PER_US);
    return (((ms * (fll_sca_ppm + CONN_SCA_LOCAL)) / 1000u) + 1u + 32u)
           * T10_PER_US;
}

/** @brief Wait for @p mask in EVENTS_DISABLED-style register @p ev, at most
 *         @p n polls; 1 when it came. */
static uint8_t conn_wait(volatile uint32_t *ev, uint32_t n)
{
    uint32_t i;
    for (i = 0u; i < n; i++) {
        if (*ev != 0u) {
            return 1u;
        }
    }
    return 0u;
}

/**
 * @brief Hold the connection captured in the conn_* fields until
 *        TIKU_FLPR_CMD_CONN_STOP, LL_TERMINATE_IND, a supervision timeout, or
 *        six intervals with no packet before the first.
 *
 * On exit the RADIO is back in its advertising configuration and
 * conn_state reads 3.
 */
static void flpr_conn_hold(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    NRF_TIMER_Type *t = NRF_TIMER10_NS;
    NRF_DPPIC_Type *d = NRF_DPPIC10_NS;
    uint32_t aa = sh->conn_aa, crcinit = sh->conn_crcinit;
    uint8_t  hop = sh->conn_hop, chmap[5];
    uint8_t  last_un = 0u, k, i;
    uint32_t event = 0u;
    uint32_t ci = (uint32_t)sh->conn_interval * 1250u * T10_PER_US;
    uint32_t to = (uint32_t)sh->conn_timeout * 10000u * T10_PER_US;
    /* The central's first packet starts anywhere in the transmit window:
     * nominal is its earliest start, span its extra length. */
    uint32_t nominal = (1250u + (uint32_t)sh->conn_winoffset * 1250u)
                     * T10_PER_US;
    uint32_t span = (uint32_t)sh->conn_winsize * 1250u * T10_PER_US;
    uint32_t last = 0u;                          /* last anchor caught       */
    uint8_t  established = 0u;
    uint16_t cec = 0u;                           /* connEventCount (wraps)    */

    for (i = 0u; i < 5u; i++) {
        chmap[i] = sh->conn_chm[i];
    }
    /* Reset the LL / L2CAP-transport state for this connection. */
    fll_tx_len = 0u; fll_tx_llid = 0u; fll_sent_vers = 0u; fll_tx_sent = 0u;
    fll_sn = 0u; fll_nesn = 0u;
    fll_a2f_seen = sh->a2f_seq;
    sh->a2f_ack  = sh->a2f_seq;                  /* TX slot free              */
    fll_cm_pending = 0u; fll_cu_pending = 0u;    /* no update armed           */
    fll_terminate = 0u;
    fll_term_sent = 0u;
    fll_reason = 0x16u;                          /* ended here, unless ...    */
    fll_enc = FLL_ENC_OFF;                       /* every link starts clear   */
    fll_rx_enc = 0u;
    fll_tx_enc = 0u;
    fll_rx_ctr = 0u;
    fll_tx_ctr = 0u;
    sh->enc_on = 0u;
    sh->dle_max = 0u;                            /* 0 = 27 octets, pre-DLE    */
    fll_phy_pending = 0u;                        /* 1M until an update        */
    sh->conn_phy = 0u; sh->conn_phy_evt = 0u;
    sh->conn_phy_mode = 0u;                      /* PHY-switch telemetry      */
    sh->conn_phy_addr = 0u; sh->conn_phy_crcok = 0u;
    sh->conn_sub = 0u;
    sh->conn_widen_us = 0u; sh->conn_late = 0u;
    sh->conn_misses = 0u; sh->conn_tx_late = 0u; sh->conn_first = 0xFFFFu;

    r->BASE0   = aa << 8;                        /* BALEN=3: base in top 3 B */
    r->PREFIX0 = (aa >> 24) & 0xFFu;
    r->CRCINIT = crcinit & 0x00FFFFFFu;

    for (;;) {
        uint32_t open, close, now, widen, t_end;
        uint8_t  got = 0u, newdata, send_pend;

        /* Updates at their Instant (wrap-safe >=): the channel map before
         * this event's CSA#1 pick, a connection update's transmit window
         * after the old interval's anchor, and a PHY switch before the RX
         * arm, all where the central switches. */
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
            nominal += (uint32_t)fll_cu_winoffset * 1250u * T10_PER_US;
            span = (uint32_t)fll_cu_winsize * 1250u * T10_PER_US;
            ci = (uint32_t)fll_cu_interval * 1250u * T10_PER_US;
            to = (uint32_t)fll_cu_timeout * 10000u * T10_PER_US;
            sh->conn_interval = fll_cu_interval;
            sh->conn_timeout  = fll_cu_timeout;
            fll_cu_pending = 0u;
            sh->conn_cu = sh->conn_cu + 1u;
        }
        if (fll_phy_pending &&
            (uint16_t)(cec - fll_phy_instant) < 0x8000u) {
            if (fll_phy_new == 2u) {             /* Coded S8: MODE 5, PLEN
                                                  * LongRange, CILEN 2,
                                                  * TERMLEN 3                */
                r->MODE  = 5u;
                r->PCNF0 = (8u << 0) | (1u << 8) | (1u << 20) |
                           (3u << 24) | (2u << 22) | (3u << 29);
            } else if (fll_phy_new == 1u) {      /* 2M                        */
                r->MODE  = 4u;
                r->PCNF0 = (8u << 0) | (1u << 8) | (1u << 20) | (1u << 24);
            } else {                             /* 1M                        */
                r->MODE  = 3u;
                r->PCNF0 = (8u << 0) | (1u << 8) | (1u << 20);
            }
            fll_phy_pending = 0u;
            sh->conn_phy = fll_phy_new;
            sh->conn_phy_evt = sh->conn_events;  /* events at the switch      */
            sh->conn_phy_mode = r->MODE;         /* MODE read back            */
        }

        /* The host asked for the link to end: an LL_TERMINATE_IND with its
         * code once the pending slot is free; the central's ack ends it. */
        if (sh->conn_term != 0u && !fll_term_sent && fll_tx_len == 0u) {
            uint8_t code = (uint8_t)sh->conn_term;
            fll_queue_ctrl(0x02u, &code, 1u);
            fll_term_sent = 1u;
        }
        /* The encryption start, a step whenever the pending slot is free:
         * LL_ENC_RSP once the M33 has SKDs and IVs, then LL_START_ENC_REQ
         * with the key (RX decrypting from here) or a refusal, and the
         * peripheral's LL_START_ENC_RSP, encrypted, once the central's is
         * in. */
        if (fll_tx_len == 0u) {
            if (fll_enc == FLL_ENC_SKD && sh->enc_rsp_seq == fll_enc_seq) {
                uint8_t rsp[12];
                for (i = 0u; i < 8u; i++) {
                    rsp[i] = sh->enc_skds[i];
                }
                for (i = 0u; i < 4u; i++) {
                    rsp[8u + i] = sh->enc_ivs[i];
                }
                fll_queue_ctrl(0x04u, rsp, 12u);     /* LL_ENC_RSP          */
                fll_enc = FLL_ENC_KEY;
            } else if (fll_enc == FLL_ENC_KEY &&
                       sh->enc_key_seq == fll_enc_seq) {
                if (sh->enc_key_status != 0u) {
                    uint8_t rej[2];
                    rej[0] = 0x03u;
                    rej[1] = sh->enc_key_status;
                    fll_queue_ctrl(0x11u, rej, 2u);  /* LL_REJECT_EXT_IND   */
                    fll_enc = FLL_ENC_OFF;
                } else {
                    for (i = 0u; i < 16u; i++) {
                        fll_sk[i] = sh->enc_sk[i];
                    }
                    for (i = 0u; i < 8u; i++) {
                        fll_iv[i] = sh->enc_iv[i];
                    }
                    fll_queue_ctrl(0x05u, (const uint8_t *)0, 0u);
                    fll_rx_enc = 1u;                 /* LL_START_ENC_REQ    */
                    fll_enc = FLL_ENC_START;
                }
            } else if (fll_enc == FLL_ENC_RSP) {
                fll_tx_enc = 1u;
                fll_queue_ctrl(0x06u, (const uint8_t *)0, 0u);
                fll_enc = FLL_ENC_DONE;
            }
        }
        /* The host's next L2CAP fragment, when the pending slot is free
         * (at most 32 bytes, with its a2f_llid); a2f_ack frees the slot.
         * The encryption start holds data back until it ends. */
        if (fll_tx_len == 0u && sh->a2f_seq != fll_a2f_seen &&
            (fll_enc == FLL_ENC_OFF || fll_enc == FLL_ENC_ON)) {
            uint8_t n = (uint8_t)sh->a2f_len, fr[32];
            fll_a2f_seen = sh->a2f_seq;
            if (n > sizeof(fr)) {
                n = (uint8_t)sizeof(fr);
            }
            for (i = 0u; i < n; i++) {
                fr[i] = sh->a2f_buf[i];
            }
            fll_queue_raw((uint8_t)sh->a2f_llid, fr, n);  /* 2 start / 1 cont */
            sh->a2f_ack = sh->a2f_seq;            /* slot free for the host   */
        }

        k = flpr_csa1_next(&last_un, hop, chmap);
        r->FREQUENCY = flpr_data_freq(k);
        r->DATAWHITE = 0x00890000u | (0x40u | k);
        /* The reply is built now, the pending PDU or an empty one: after
         * the central's packet only its header changes, within the 140 us
         * before the radio reads it. */
        (void)fll_build_tx(conn_txb);

        /* The window: the nominal start widened by the drift since the
         * last packet caught, early by the RX ramp, and open past the
         * latest start for the access address to arrive. */
        widen = conn_widen(nominal - last);
        open  = nominal - widen - CONN_RX_EARLY_US * T10_PER_US;
        close = nominal + span + widen +
                (conn_aa_us(sh->conn_phy) + CONN_RX_LATE_US) * T10_PER_US;
        for (;;) {
            now = t10_now();
            if ((int32_t)(now - open) >= 0) {
                break;
            }
            if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
                break;
            }
        }
        if (sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
            break;
        }
        if ((int32_t)(now - nominal) > 0) {
            sh->conn_late = sh->conn_late + 1u;  /* opened past its start    */
        }
        sh->conn_widen_us = widen / T10_PER_US;

        r->SHORTS = (1u << 0) | (1u << 19);      /* READY_START, PHYEND_DIS. */
        r->PACKETPTR = (uint32_t)conn_datrx;
        r->EVENTS_ADDRESS  = 0u;
        r->EVENTS_PHYEND   = 0u;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_CRCOK    = 0u;
        r->EVENTS_CRCERROR = 0u;
        (void)r->EVENTS_DISABLED;
        r->TASKS_RXEN = 1u;
        while ((int32_t)(t10_now() - close) < 0) {
            if (r->EVENTS_ADDRESS != 0u) {
                got = 1u;
                break;
            }
        }
        if (!got) {
            r->TASKS_DISABLE = 1u;
            (void)conn_wait(&r->EVENTS_DISABLED, 8000u);
            /* Missed: keep the schedule (the central hops on regardless),
             * and the transmit window's uncertainty until a first catch. */
            sh->conn_misses = sh->conn_misses + 1u;
            nominal += ci;
            cec++;
            if (!established) {
                if (cec >= 6u) {
                    fll_reason = 0x3Eu;
                    break;                       /* never established        */
                }
            } else if ((int32_t)(nominal - last) > (int32_t)to) {
                fll_reason = 0x08u;
                break;                           /* supervision timeout      */
            }
            continue;
        }

        /* A packet: its end arms the reply 150 us later through
         * COMPARE[0]; the CRC verdict follows the end. */
        (void)conn_wait(&r->EVENTS_PHYEND,
                        (sh->conn_phy == 2u) ? 400000u : 40000u);
        t_end = t->CC[3];
        t->CC[0] = t_end + CONN_REPLY_TICKS;
        t->EVENTS_COMPARE[0] = 0u;
        d->CHENSET = (1u << FLPR_DPPI_CH_TXEN);
        r->EVENTS_READY = 0u;                    /* the reply's ramp is next */
        r->PACKETPTR = (uint32_t)conn_txb;       /* read at the reply's START */
        (void)conn_wait(&r->EVENTS_DISABLED, 8000u);
        r->EVENTS_DISABLED = 0u;
        __asm__ volatile ("fence iorw, iorw" ::: "memory");
        newdata = 0u;
        send_pend = (uint8_t)(fll_tx_len != 0u); /* the pre-built carries it */
        if (r->EVENTS_CRCOK != 0u) {
            uint8_t h = conn_datrx[0];
            /* An L2CAP fragment needs the f2a slot, which holds one until
             * the M33 takes it. */
            uint8_t room = (uint8_t)(conn_datrx[1] == 0u ||
                                     (h & 0x03u) == 0x03u ||
                                     sh->f2a_ack == sh->f2a_seq);
            uint8_t rc = flpr_ll_ack(&fll_sn, &fll_nesn,
                                     (uint8_t)((h >> 3) & 1u),
                                     (uint8_t)((h >> 2) & 1u),
                                     (uint8_t)(conn_datrx[1] != 0u), room);
            if ((rc & 2u) && fll_tx_sent) {
                /* The pending PDU landed (an ack of an empty PDU sent
                 * before it was queued leaves it pending): this reply is
                 * an empty PDU, and the next PDU goes at the next event. */
                fll_tx_len = 0u;
                fll_tx_sent = 0u;
                send_pend = 0u;
                conn_txb[1] = 0u;
                conn_txb[0] = 0x01u;
                if (fll_term_sent) {
                    fll_terminate = 1u;          /* the TERMINATE_IND landed */
                }
                if (fll_enc == FLL_ENC_DONE) {
                    fll_enc = FLL_ENC_ON;        /* the START_ENC_RSP landed */
                    sh->enc_on = 1u;
                }
            }
            conn_txb[0] = (uint8_t)((conn_txb[0] & 0x03u) |
                                    (fll_nesn << 2) | (fll_sn << 3));
            if (conn_txb[1] == 0u) {
                conn_txb[2] = conn_txb[0];
            }
            newdata = (uint8_t)(rc & 1u);
            /* The anchor: the packet's start, ahead of its ADDRESS. */
            last = t->CC[4] - conn_aa_us(sh->conn_phy) * T10_PER_US;
            nominal = last;
            span = 0u;
            if (!established) {
                sh->conn_first = cec;
            }
            established = 1u;
            if (sh->conn_phy != 0u) {
                sh->conn_phy_crcok = sh->conn_phy_crcok + 1u;
            }
        }
        if (sh->conn_phy != 0u) {                /* ADDRESS off the 1M PHY    */
            sh->conn_phy_addr = sh->conn_phy_addr + 1u;
        }
        if (!conn_wait(&r->EVENTS_READY, 20000u)) {
            r->TASKS_DISABLE = 1u;               /* TXEN never came          */
            sh->conn_tx_late = sh->conn_tx_late + 1u;
        } else {
            fll_tx_sent = send_pend;             /* what the peer acks next  */
        }
        (void)conn_wait(&r->EVENTS_DISABLED, 40000u);
        d->CHENCLR = (1u << FLPR_DPPI_CH_TXEN);
        /* The received payload, now that the reply is out: LL control is
         * answered at the next event, L2CAP goes to the M33 host. */
        if (newdata) {
            if (fll_rx_enc) {                    /* decrypted, or the end   */
                uint8_t n = conn_datrx[1];
                if (n < 5u || !flpr_ccm(1u, 1u, fll_rx_ctr, conn_datrx[0],
                                        &conn_datrx[3], (uint8_t)(n - 4u))) {
                    fll_reason = 0x3Du;          /* MIC failure             */
                    break;
                }
                conn_datrx[1] = (uint8_t)(n - 4u);
                fll_rx_ctr++;
            }
            fll_handle_rx(conn_datrx, sh);
        }
        sh->conn_events = ++event;
        nominal += ci;
        cec++;
        if (fll_terminate || sh->cmd == TIKU_FLPR_CMD_CONN_STOP) {
            break;
        }
        if (established && (int32_t)(nominal - last) > (int32_t)to) {
            fll_reason = 0x08u;
            break;                               /* only bad CRCs: timeout   */
        }
    }
    r->TASKS_DISABLE = 1u;
    (void)conn_wait(&r->EVENTS_DISABLED, 8000u);
    /* Restore the advertising configuration before the radio is handed on.
     * A connection sets the access address (BASE0/PREFIX0), the CRC init
     * and, after a PHY switch, MODE/PCNF0 to its own values, and the next
     * owner inherits them.  The M33 runs its advertising init once a boot,
     * so a beacon after a connection would otherwise transmit frames no
     * scanner decodes (scanners filter for 0x8E89BED6 / 0x555555 on 1M). */
    r->MODE    = 3u;
    r->PCNF0   = (8u << 0) | (1u << 8) | (1u << 20);
    r->BASE0   = 0x89BED600u;                    /* access addr 0x8E89BED6   */
    r->PREFIX0 = 0x0000008Eu;
    r->CRCINIT = 0x00555555u;                    /* advertising CRC init     */
    sh->conn_reason = fll_reason;
    sh->enc_on = 0u;                             /* none for the next link   */
    sh->conn_state = 3u;                         /* link ended               */
}

/** @brief Advertising gap at 2 MHz: 20 ms plus a 0..10 ms delay. */
static uint32_t flpr_adv_gap_ticks(uint32_t random)
{
    return 40000u + ((random >> 8) % 20001u);
}

/* TIMER10 times the SCAN_RSP.  Every PHYEND clears the timer over DPPI, and
 * COMPARE[0] fires the RADIO's TXEN FLPR_ADV_TXEN_TICKS ticks (2 MHz) after
 * a request ends, on a DPPI channel opened only for a request this
 * advertiser answers.  The RADIO's TIFS does not govern the PHYEND_DISABLE +
 * DISABLED_TXEN chain on this part: that chain replies at the TX ramp's
 * pace, well before the 150 +/- 2 us a scanner listens at.  200 ticks is the
 * value a second radio scanning this one (`bleadv scanreq`) measures as
 * 150 us; this advertiser's own capture of its reply (adv_tifs) reads about
 * 40 ticks more, since its receive and transmit events fire at different
 * points in a packet. */
#define FLPR_ADV_TXEN_TICKS  200u

/** @brief Wire TIMER10 and DPPIC10 for the timed reply (or unwire, on 0). */
static void flpr_conn_adv_timing(NRF_RADIO_Type *r, uint32_t txen_ticks)
{
    NRF_TIMER_Type *t = NRF_TIMER10_NS;
    NRF_DPPIC_Type *d = NRF_DPPIC10_NS;

    if (txen_ticks == 0u) {
        r->PUBLISH_PHYEND  = 0u;
        r->PUBLISH_ADDRESS = 0u;
        r->SUBSCRIBE_TXEN  = 0u;
        t->SUBSCRIBE_CLEAR      = 0u;
        t->SUBSCRIBE_CAPTURE[3] = 0u;
        t->SUBSCRIBE_CAPTURE[4] = 0u;
        t->PUBLISH_COMPARE[0]   = 0u;
        d->CHENCLR = (1u << FLPR_DPPI_CH_PHYEND) | (1u << FLPR_DPPI_CH_ADDR) |
                     (1u << FLPR_DPPI_CH_TXEN);
        t->TASKS_STOP = 1u;
        return;
    }
    t->TASKS_STOP  = 1u;
    t->TASKS_CLEAR = 1u;
    t->MODE      = 0u;
    t->BITMODE   = 3u;
    t->PRESCALER = 4u;
    t->SHORTS    = 0u;
    t->CC[0] = txen_ticks;
    t->EVENTS_COMPARE[0] = 0u;
    t->SUBSCRIBE_CLEAR      = FLPR_DPPI_CH_PHYEND | (1u << 31);
    t->SUBSCRIBE_CAPTURE[4] = FLPR_DPPI_CH_ADDR   | (1u << 31);
    t->PUBLISH_COMPARE[0]   = FLPR_DPPI_CH_TXEN   | (1u << 31);
    r->PUBLISH_PHYEND  = FLPR_DPPI_CH_PHYEND | (1u << 31);
    r->PUBLISH_ADDRESS = FLPR_DPPI_CH_ADDR   | (1u << 31);
    r->SUBSCRIBE_TXEN  = FLPR_DPPI_CH_TXEN   | (1u << 31);
    d->CHENCLR = (1u << FLPR_DPPI_CH_TXEN);
    d->CHENSET = (1u << FLPR_DPPI_CH_PHYEND) | (1u << FLPR_DPPI_CH_ADDR);
    t->TASKS_START = 1u;
}

/**
 * @brief Is the packet just received a SCAN_REQ addressed to this advertiser?
 *
 * Runs inside the T_IFS window the reply has to meet, so it decides on the
 * length byte before touching the address: a CONNECT_IND (34) and a SCAN_REQ
 * (12) part company on the second byte.
 *
 * @param addr the advertiser address to match at conn_rx[9..14].
 * @return 1 when a SCAN_RSP is owed, 0 otherwise.
 */
static uint8_t flpr_conn_scanreq_for(const uint8_t *addr)
{
    uint32_t i;

    if (NRF_RADIO_NS->EVENTS_CRCOK == 0u ||
        conn_rx[1] != 12u || (conn_rx[0] & 0x0Fu) != 0x03u) {
        return 0u;
    }
    for (i = 0u; i < 6u; i++) {
        if (conn_rx[9u + i] != addr[i]) {
            return 0u;
        }
    }
    return 1u;
}

/**
 * @brief Advertise the tiku_flpr_conn_t in a2f_buf on channels 37..39 for up
 *        to 4000 channel attempts, answering addressed SCAN_REQs.
 *
 * A CONNECT_IND for this AdvA is parsed into the conn_* fields, conn_state
 * goes to 1 and the link is held; giving up or a new command leaves
 * conn_state at 2.
 */
static void flpr_conn_adv(tiku_flpr_shared_t *sh)
{
    NRF_RADIO_Type *r = NRF_RADIO_NS;
    const volatile tiku_flpr_conn_t *in =
        (const volatile tiku_flpr_conn_t *)sh->a2f_buf;
    uint8_t  addr[6];
    uint32_t alen = in->adv_len, rlen, i, spin, attempt;
    uint32_t lcg;
    uint8_t  chan = 0u, connected = 0u;

    /* advDelay: the Core spec requires a 0..10 ms pseudo-random delay per
     * advertising event.  Without one, this fixed-pace loop and a central's
     * fixed scan-window rotation can lock in antiphase and miss each other
     * for a whole session.  The LCG is seeded from mcycle, which
     * tiku_flpr_main() un-inhibits, so each session differs. */
    __asm__ volatile ("csrr %0, mcycle" : "=r"(lcg));
    lcg |= 1u;

    if (alen > sizeof(conn_adv)) {
        alen = sizeof(conn_adv);
    }
    for (i = 0u; i < alen; i++) {
        conn_adv[i] = in->adv[i];
    }
    rlen = in->rsp_len;
    if (rlen == 0u || rlen > sizeof(conn_rsp)) {
        rlen = alen;                             /* no response given: mirror */
        for (i = 0u; i < rlen; i++) {
            conn_rsp[i] = in->adv[i];
        }
    } else {
        for (i = 0u; i < rlen; i++) {
            conn_rsp[i] = in->rsp[i];
        }
    }
    conn_rsp[0] = 0x44u;                         /* SCAN_RSP, TxAdd = random */
    for (i = 0u; i < 6u; i++) {
        addr[i] = in->addr[i];
    }
    sh->conn_state = 0u;
    sh->conn_events = 0u;
    sh->conn_cm = 0u;                            /* LL update counts          */
    sh->conn_cu = 0u;
    sh->adv_tx = 0u;
    sh->adv_scanreq = 0u;
    sh->adv_scanrsp = 0u;
    sh->adv_rxother = 0u;
    sh->adv_tifs = 0u;
    flpr_hfclk_kick();
    flpr_conn_adv_timing(r, (in->txen_ticks != 0u) ? in->txen_ticks
                                                   : FLPR_ADV_TXEN_TICKS);

    for (attempt = 0u; attempt < 4000u && !connected; attempt++) {
        /* An HFCLK kick per 3-channel cycle: one kick's 16-byte UARTE
         * transfer (about 1.4 ms at 115200 baud) holds the clock for one
         * cycle, not for a whole advertising session. */
        if (chan == 0u) {
            flpr_hfclk_kick();
        }
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
        sh->adv_tx++;
        /* Hand the RX leg its buffer.  The packet's end disables the radio
         * and restarts TIMER10; whether the compare then fires a reply is
         * decided below, within T_IFS. */
        r->SHORTS = (1u << 0) | (1u << 19) | (1u << 4);
        r->PACKETPTR = (uint32_t)conn_rx;
        r->EVENTS_PHYEND   = 0u;
        r->EVENTS_END      = 0u;
        r->EVENTS_DISABLED = 0u;
        r->EVENTS_CRCOK    = 0u;
        NRF_TIMER10_NS->EVENTS_COMPARE[0] = 0u;
        (void)r->EVENTS_PHYEND;
        for (spin = 0u; spin < 20000u; spin++) {
            if (r->EVENTS_PHYEND != 0u) {
                break;                          /* a packet ended           */
            }
        }
        if (r->EVENTS_PHYEND != 0u) {
            /* The DMA writes conn_rx behind the compiler: without the fence
             * a header loaded for the previous packet is reused for this
             * one. */
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            __asm__ volatile ("fence iorw, iorw" ::: "memory");
        }
        if (r->EVENTS_PHYEND == 0u) {
            r->TASKS_DISABLE = 1u;              /* window idle: rotate      */
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
        } else if (flpr_conn_scanreq_for(addr)) {
            /* A SCAN_REQ this advertiser owes an answer: hand the DMA the
             * SCAN_RSP and open the channel that lets the compare fire
             * TXEN; close it once TXEN has fired, so the reply's own end
             * chains nothing. */
            r->PACKETPTR = (uint32_t)conn_rsp;
            r->EVENTS_READY = 0u;
            r->EVENTS_DISABLED = 0u;
            (void)r->EVENTS_DISABLED;
            NRF_DPPIC10_NS->CHENSET = (1u << FLPR_DPPI_CH_TXEN);
            sh->adv_scanreq++;
            for (spin = 0u; spin < 20000u; spin++) {
                if (r->EVENTS_READY != 0u) {
                    break;
                }
            }
            NRF_DPPIC10_NS->CHENCLR = (1u << FLPR_DPPI_CH_TXEN);
            for (spin = 0u; spin < 40000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {
                    break;
                }
            }
            if (r->EVENTS_DISABLED != 0u) {
                sh->adv_scanrsp++;
                sh->adv_tifs = NRF_TIMER10_NS->CC[4];
            } else {
                r->TASKS_DISABLE = 1u;          /* TXEN never came          */
                for (spin = 0u; spin < 8000u; spin++) {
                    if (r->EVENTS_DISABLED != 0u) {
                        break;
                    }
                }
            }
        } else if (r->EVENTS_CRCOK != 0u &&
                   (conn_rx[0] & 0x0Fu) == 0x05u && conn_rx[1] == 34u) {
            uint8_t match = 1u;
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {   /* the short's disable  */
                    break;
                }
            }
            __asm__ volatile ("fence iorw, iorw" ::: "memory");
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
                sh->conn_latency = (uint16_t)(conn_rx[27] |
                                              ((uint16_t)conn_rx[28] << 8));
                sh->conn_timeout = (uint16_t)(conn_rx[29] |
                                              ((uint16_t)conn_rx[30] << 8));
                sh->conn_sca = (uint8_t)((conn_rx[36] >> 5) & 0x07u);
                for (i = 0u; i < 5u; i++) {
                    sh->conn_chm[i] = conn_rx[31u + i];
                }
                sh->conn_hop = (uint8_t)(conn_rx[36] & 0x1Fu);
                fll_sca_ppm = fll_sca_table[(conn_rx[36] >> 5) & 0x07u];
                /* Peer identity for SMP f5/f6: InitA at [3..8], AdvA at
                 * [9..14]; header bit6 (TxAdd) = InitA type, bit7 (RxAdd) =
                 * AdvA type. */
                for (i = 0u; i < 6u; i++) {
                    sh->conn_inita[i] = conn_rx[3u + i];
                    sh->conn_adva[i]  = conn_rx[9u + i];
                }
                sh->conn_addr_types =
                    (uint8_t)(((conn_rx[0] & 0x40u) ? 0x01u : 0x00u) |
                              ((conn_rx[0] & 0x80u) ? 0x02u : 0x00u));
                connected = 1u;
            }
        } else {
            for (spin = 0u; spin < 8000u; spin++) {
                if (r->EVENTS_DISABLED != 0u) {   /* the short's disable  */
                    break;
                }
            }
            if (r->EVENTS_CRCOK != 0u) {
                sh->adv_rxother++;              /* someone else's traffic   */
            }
        }
        if (!connected) {
            chan = (uint8_t)((chan + 1u) % 3u);
            /* The gap, and the advDelay after each 3-channel event. */
            if (chan == 0u) {
                uint32_t d;
                lcg = lcg * 1103515245u + 12345u;
                NRF_TIMER_Type *t = NRF_TIMER10_NS;
                uint32_t start;
                d = flpr_adv_gap_ticks(lcg);
                t->TASKS_CAPTURE[1] = 1u;
                start = t->CC[1];
                do {
                    if (sh->cmd != 0u) {
                        break;
                    }
                    t->TASKS_CAPTURE[1] = 1u;
                } while ((uint32_t)(t->CC[1] - start) < d);
            }
        }
        if (sh->cmd != 0u) {                     /* honour STOP / new cmd    */
            break;
        }
    }
    if (connected) {
        /* TIMER10 keeps counting from the CONNECT_IND's end: PHYEND now
         * captures into CC[3] instead of clearing it, and COMPARE[0] fires
         * TXEN only when an event arms it. */
        NRF_DPPIC10_NS->CHENCLR = (1u << FLPR_DPPI_CH_TXEN);
        NRF_TIMER10_NS->SUBSCRIBE_CLEAR      = 0u;
        NRF_TIMER10_NS->SUBSCRIBE_CAPTURE[3] = FLPR_DPPI_CH_PHYEND | (1u << 31);
        sh->conn_state = 1u;                     /* connected -> M33 sees it */
        flpr_conn_hold(sh);                      /* then hold autonomously   */
    } else {
        sh->conn_state = 2u;                     /* gave up advertising      */
    }
    flpr_conn_adv_timing(r, 0u);                 /* nothing may fire TXEN    */
}

void tiku_flpr_main(void);

/**
 * @brief Trap landing: record the fault, park, wait for a restart order.
 *
 * Writes the fault magic, cause and epc, then calls tiku_flpr_main() again
 * on TIKU_FLPR_CMD_RESTART.  Re-setting CPURUN resumes at the current PC,
 * so this call is the only way to restart the payload.
 */
__attribute__((used, noreturn))
void tiku_flpr_fault(void)
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    uint32_t cause, epc;

    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile ("csrr %0, mepc"   : "=r"(epc));
    sh->fault_cause = cause;
    sh->fault_epc   = epc;
    sh->fault_count = sh->fault_count + 1u;
    sh->magic = TIKU_FLPR_MAGIC_FAULT;
    __asm__ volatile ("fence" ::: "memory");

    while (sh->cmd != TIKU_FLPR_CMD_RESTART) {
    }
    sh->cmd = 0u;
    sh->rsp = 0u;
    /* Re-run the payload by calling its entry.  With MIE clear and no
     * interrupts, a trap leaves only mepc and mcause set, so a plain call is
     * enough; tiku_flpr_main() writes the magic again.  flpr_trap reset the
     * stack; gp is still valid and .bss keeps its values. */
    tiku_flpr_main();
    for (;;) {
    }
}

/**
 * @brief Echo service: mirror a new app->flpr message back and raise
 *        doorbell channel 16 to the app core.
 */
static void flpr_echo_pump(tiku_flpr_shared_t *sh, uint32_t *last_seq)
{
    uint32_t seq = sh->a2f_seq;
    uint32_t len, i;

    if (seq == *last_seq) {
        return;
    }
    *last_seq = seq;
    /* The M33 writes buf and len before seq; the fence keeps this core's
     * reads in the same order, so len belongs to this message and not the
     * previous one. */
    __asm__ volatile ("fence" ::: "memory");
    len = sh->a2f_len;
    if (len > TIKU_FLPR_MSG_CAP) {
        len = TIKU_FLPR_MSG_CAP;
    }
    /* The 4-byte message FLT! executes an illegal instruction, so a test
     * can fault the payload; the trap lands in tiku_flpr_fault() through
     * mtvec. */
    if (len == 4u &&
        sh->a2f_buf[0] == (uint8_t)'F' && sh->a2f_buf[1] == (uint8_t)'L' &&
        sh->a2f_buf[2] == (uint8_t)'T' && sh->a2f_buf[3] == (uint8_t)'!') {
        __asm__ volatile ("unimp");
    }
    for (i = 0u; i < len; i++) {
        sh->f2a_buf[i] = sh->a2f_buf[i];
    }
    sh->f2a_len = len;
    sh->f2a_seq = sh->f2a_seq + 1u;
    flpr_doorbell_to_app();
}

/**
 * @brief Payload entry from crt0 (and from tiku_flpr_fault() on a restart):
 *        publish the magic, then serve the command word forever.
 */
void tiku_flpr_main(void)
{
    tiku_flpr_shared_t *sh = TIKU_FLPR_SHARED;
    volatile uint32_t pace;
    /* last_seq starts at the mailbox's current sequence: after a fault
     * restart the FLT! message is still in the buffer, and consuming it
     * again would fault the payload again. */
    uint32_t last_seq = sh->a2f_seq;

    beacon_on = 0u;

    /* Enable the VPR's RT-peripheral interface (keyed VPRNORDICCTRL CSR,
     * 0x7C0: NORDICKEY=0x507D<<16 | ENABLERTPERIPH).  Until this runs, the
     * VEVIF half of VPR00 (tasks, events, INTEN; offsets below 0x800) reads
     * zero and ignores writes from both sides.  Host-side control (INITPC,
     * CPURUN; 0x800 and up) works regardless. */
    __asm__ volatile ("csrw 0x7C0, %0" :: "r"((0x507Du << 16) | 1u));

    /* Un-inhibit the machine counters (mcountinhibit, 0x320) so mcycle
     * counts: flpr_conn_adv() seeds its advDelay from it, and a RISC-V core
     * may reset with the counters gated. */
    __asm__ volatile ("csrw 0x320, zero");

    sh->heartbeat = 0u;
    sh->magic = TIKU_FLPR_MAGIC;

    for (;;) {
        /* Park (see tiku_flpr_ipc.h): the heartbeat stops while parked and
         * RESUME returns to this loop.  The parked wait is a busy poll: no
         * interrupt source is wired to the VPR, so a WFI here would never
         * wake. */
        if (sh->cmd == TIKU_FLPR_CMD_PARK) {
            sh->rsp = TIKU_FLPR_RSP_PARKED;
            while (sh->cmd != TIKU_FLPR_CMD_RESUME) {
                for (pace = 0u; pace < 8000u; pace++) {
                }
            }
            sh->cmd = 0u;
            sh->rsp = 0u;
        }
        if (sh->cmd == TIKU_FLPR_CMD_SPIN) {
            /* Compute-only load: a register-only inner loop of 4096
             * iterations with one shared-memory store per pass, so the
             * current is this core's execution.  .spin_iters is re-read every
             * pass, so the app core ends the load early by writing 0. */
            uint32_t done = 0u;
            sh->spin_passes = 0u;
            sh->cmd = 0u;
            while (done < sh->spin_iters) {
                register uint32_t n = 4096u;
                __asm__ volatile (".p2align 4\n"
                                  "1: addi %0, %0, -1\n\t"
                                  "   bnez %0, 1b\n"
                                  : "+r" (n) : : );
                done++;
                sh->spin_passes = done;
            }
            sh->rsp = TIKU_FLPR_RSP_SPIN_DONE;
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
            beacon_pace_iters = b->pace_iters;
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
            uint32_t remaining = beacon_pace_iters;
            flpr_beacon_burst();
            sh->beacon_bursts = sh->beacon_bursts + 1u;
            sh->heartbeat = sh->heartbeat + 1u;
            /* Check for STOP or PARK after at most 12800 iterations. */
            while (remaining != 0u) {
                volatile uint32_t s;
                uint32_t slice = remaining < 12800u ? remaining : 12800u;
                for (s = 0u; s < slice; s++) {
                }
                remaining -= slice;
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
