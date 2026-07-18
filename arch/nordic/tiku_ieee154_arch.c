/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ieee154_arch.c - IEEE 802.15.4-2006 250 kbps O-QPSK PHY on the
 *                       nRF54L on-die RADIO, from scratch (N1 bring-up).
 *
 * The RADIO's 15.4 mode (MODE=0xF) does the O-QPSK/DSSS PHY and the SFD
 * sync in hardware; software programs the packet format and drives
 * TXEN/RXEN, exactly like the BLE path in tiku_radio_arch.c.  The frame in
 * RAM is [PHR][PSDU]: PHR is the 8-bit LENGTH and -- because CRCINC=Include
 * -- it counts the 2-byte FCS the radio computes/checks.  No whitening
 * (WHITEEN=0) and no access address (BALEN=0): the 4-symbol zero preamble
 * plus the 0xA7 SFD provide sync.
 *
 * PHY only: raw frame in / out, energy detect, CCA (N2 will add MAC).  The
 * HFXO start and Constant-Latency (erratum 20) discipline are borrowed from
 * the BLE arch so both stay consistent on this shared peripheral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_ieee154_arch.h>
#include <arch/nordic/tiku_radio_arch.h>       /* constlat, hfclk, BLE restore */
#include <arch/nordic/tiku_device_select.h>    /* MDK register types + RADIO   */
#include <arch/nordic/tiku_timer_arch.h>       /* TIKU_CLOCK_ARCH_SECOND first  */
#include <kernel/timers/tiku_clock.h>          /* RX wall-clock timeout        */
#include <kernel/cpu/tiku_watchdog.h>          /* kick during a long listen    */
#include <string.h>

#define RADIO  NRF_RADIO_S

/* CRC-16-CCITT (x^16+x^12+x^5+1), init 0 -- the 802.15.4 FCS. */
#define TIKU_154_CRC_POLY  0x00011021u
#define TIKU_154_CRC_INIT  0x00000000u
#define TIKU_154_SFD       0xA7u

/* EasyDMA frame buffers: [PHR][up to 126 B].  Word-aligned for EasyDMA. */
static uint8_t tx_frame[1u + TIKU_154_MAX_FRAME] __attribute__((aligned(4)));
static uint8_t rx_frame[1u + TIKU_154_MAX_FRAME] __attribute__((aligned(4)));
static uint8_t cur_chan = TIKU_154_CHAN_MIN;

int tiku_ieee154_arch_available(void)
{
    return 1;
}

/* Program the full 15.4 link config + tune to a channel.  Leaves the radio
 * DISABLED with no SHORTS armed. */
static void radio_154_linkcfg(uint8_t channel)
{
    if (channel < TIKU_154_CHAN_MIN) {
        channel = TIKU_154_CHAN_MIN;
    } else if (channel > TIKU_154_CHAN_MAX) {
        channel = TIKU_154_CHAN_MAX;
    }
    cur_chan = channel;

    RADIO->MODE = RADIO_MODE_MODE_Ieee802154_250Kbit;      /* 0xF */
    /* [PHR: 8-bit LENGTH][PSDU]; 32-bit zero preamble; LENGTH counts the
     * FCS (CRCINC).  No S0/S1. */
    RADIO->PCNF0 = ((uint32_t)8u << RADIO_PCNF0_LFLEN_Pos) |
        ((uint32_t)RADIO_PCNF0_PLEN_32bitZero << RADIO_PCNF0_PLEN_Pos) |
        ((uint32_t)RADIO_PCNF0_CRCINC_Include << RADIO_PCNF0_CRCINC_Pos);
    /* MAXLEN 127; BALEN 0 (no access address); little-endian; WHITEEN 0. */
    RADIO->PCNF1 = ((uint32_t)TIKU_154_MAX_FRAME << RADIO_PCNF1_MAXLEN_Pos);

    RADIO->CRCCNF =
        ((uint32_t)RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
        ((uint32_t)RADIO_CRCCNF_SKIPADDR_Ieee802154 <<
         RADIO_CRCCNF_SKIPADDR_Pos);
    RADIO->CRCPOLY = TIKU_154_CRC_POLY;
    RADIO->CRCINIT = TIKU_154_CRC_INIT;
    RADIO->SFD     = TIKU_154_SFD;

    RADIO->TXPOWER = tiku_radio_arch_txpower_code();        /* shared knob */
    /* Channel k -> 2405 + 5(k-11) MHz; FREQUENCY register is MHz-2400. */
    RADIO->FREQUENCY = 5u + (5u * (uint32_t)(channel - TIKU_154_CHAN_MIN));
    RADIO->SHORTS = 0u;
}

void tiku_ieee154_arch_mode_154(uint8_t channel)
{
    radio_154_linkcfg(channel);
}

void tiku_ieee154_arch_mode_ble(void)
{
    tiku_radio_arch_init();                     /* restore BLE link config    */
}

void tiku_ieee154_arch_set_channel(uint8_t channel)
{
    if (channel < TIKU_154_CHAN_MIN) {
        channel = TIKU_154_CHAN_MIN;
    } else if (channel > TIKU_154_CHAN_MAX) {
        channel = TIKU_154_CHAN_MAX;
    }
    cur_chan = channel;
    RADIO->FREQUENCY = 5u + (5u * (uint32_t)(channel - TIKU_154_CHAN_MIN));
}

int tiku_ieee154_arch_tx(const uint8_t *psdu, uint8_t len)
{
    uint32_t spin;

    if (len > TIKU_154_MAX_PSDU) {
        return -1;
    }
    tx_frame[0] = (uint8_t)(len + 2u);          /* PHR: payload + 2-byte FCS  */
    if (len != 0u) {
        memcpy(&tx_frame[1], psdu, len);
    }
    RADIO->PACKETPTR = (uint32_t)tx_frame;
    RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_READY_START_Pos) |
                    ((uint32_t)1u << RADIO_SHORTS_PHYEND_DISABLE_Pos);
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_DISABLED = 0u;

    tiku_radio_arch_hfclk_kick();               /* HFXO up (erratum-safe)     */
    RADIO->TASKS_TXEN = 1u;
    for (spin = 0u; spin < 2000000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    return (RADIO->EVENTS_DISABLED != 0u) ? 0 : -2;
}

int tiku_ieee154_arch_rx(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                         int8_t *rssi)
{
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t dl =
        (tiku_clock_time_t)(((uint32_t)TIKU_CLOCK_SECOND * timeout_ms) /
                            1000u);
    uint32_t spin = 0u;
    uint8_t  got = 0u;

    RADIO->PACKETPTR = (uint32_t)rx_frame;
    RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_READY_START_Pos);
    RADIO->EVENTS_END      = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->EVENTS_DISABLED = 0u;

    tiku_radio_arch_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;

    for (;;) {
        if (RADIO->EVENTS_END != 0u) {
            got = 1u;
            break;
        }
        spin++;
        if ((spin & 0x3FFu) == 0u) {
            RADIO->TASKS_RSSISTART = 1u;        /* keep a fresh RSSI sample   */
        }
        if ((spin & 0xFFFFu) == 0u) {           /* ~every 64k iters           */
            tiku_radio_arch_hfclk_kick();       /* re-arm HFXO across listen  */
            tiku_watchdog_kick();
            if ((tiku_clock_time_t)(tiku_clock_time() - start) >= dl) {
                break;                          /* listen window elapsed      */
            }
        }
    }
    if (rssi != 0) {
        *rssi = (int8_t)(-(int)(RADIO->RSSISAMPLE & 0x7Fu));
    }
    RADIO->SHORTS = 0u;
    RADIO->EVENTS_DISABLED = 0u;
    RADIO->TASKS_DISABLE = 1u;
    for (spin = 0u; spin < 200000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    if (!got) {
        return 0;                               /* timeout                    */
    }
    if (RADIO->CRCSTATUS != RADIO_CRCSTATUS_CRCSTATUS_CRCOk) {
        return -1;                              /* frame with bad FCS         */
    }
    {
        uint8_t phr  = rx_frame[0];
        uint8_t plen = (phr >= 2u) ? (uint8_t)(phr - 2u) : 0u;  /* strip FCS  */
        if (plen > cap) {
            plen = cap;
        }
        if (plen != 0u) {
            memcpy(buf, &rx_frame[1], plen);
        }
        return (int)plen;
    }
}

int tiku_ieee154_arch_ed(uint8_t channel, int8_t *dbm)
{
    uint32_t spin;
    int lvl = -1;

    radio_154_linkcfg(channel);
    /* EDPERIOD is fixed at 0x20 (128 us) on this silicon (Min==Max==Default);
     * leaving it 0 makes every ED period zero-length and EDEND never fires. */
    RADIO->EDCTRL = ((uint32_t)8u << RADIO_EDCTRL_EDCNT_Pos) |
                    ((uint32_t)0x20u << RADIO_EDCTRL_EDPERIOD_Pos);
    /* Chain the whole measurement in hardware: RXEN ramps to READY, the
     * READY_EDSTART short kicks the ED sample, EDEND_DISABLE tears the radio
     * down again.  Polling READY then issuing EDSTART in software raced the
     * ramp and the sample never ran (level was always -1). */
    RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_READY_EDSTART_Pos) |
                    ((uint32_t)1u << RADIO_SHORTS_EDEND_DISABLE_Pos);
    RADIO->EVENTS_EDEND    = 0u;
    RADIO->EVENTS_DISABLED = 0u;

    tiku_radio_arch_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;
    for (spin = 0u; spin < 4000000u; spin++) {
        if (RADIO->EVENTS_EDEND != 0u) {
            break;
        }
        if ((spin & 0xFFFFu) == 0u) {
            tiku_watchdog_kick();               /* never wedge on a stuck ED  */
        }
    }
    if (RADIO->EVENTS_EDEND != 0u) {
        lvl = (int)(RADIO->EDSAMPLE & 0xFFu);
    }
    for (spin = 0u; spin < 200000u; spin++) {   /* let EDEND_DISABLE settle   */
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    RADIO->SHORTS = 0u;
    if (lvl >= 0 && dbm != 0) {
        *dbm = (int8_t)(-94 + lvl);             /* approx nRF ED level -> dBm */
    }
    return lvl;
}

/* ACK frame template in EasyDMA form: [PHR][FCF_lo][FCF_hi][seq].  PHR = 5
 * (3-byte MHR + 2-byte FCS, counted via CRCINC); FCF 0x0002 = ACK, no
 * addressing.  Only [3] (seq) changes per frame. */
static uint8_t ack_tmpl[4] __attribute__((aligned(4))) =
    { 5u, 0x02u, 0x00u, 0u };

int tiku_ieee154_arch_rx_ack(uint8_t *buf, uint8_t cap, uint32_t timeout_ms,
                             int8_t *rssi, uint16_t my_pan, uint16_t my_addr,
                             uint8_t *did_ack)
{
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t dl =
        (tiku_clock_time_t)(((uint32_t)TIKU_CLOCK_SECOND * timeout_ms) /
                            1000u);
    uint32_t spin = 0u;
    uint8_t  got = 0u, ack = 0u;

    RADIO->TIFS = 192u;                          /* aTurnaroundTime (12 sym)   */
    RADIO->PACKETPTR = (uint32_t)rx_frame;
    /* RX with the turnaround pre-armed; we commit or abort in the window. */
    RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_READY_START_Pos) |
                    ((uint32_t)1u << RADIO_SHORTS_PHYEND_DISABLE_Pos) |
                    ((uint32_t)1u << 2);         /* DISABLED_TXEN              */
    RADIO->EVENTS_END      = 0u;
    RADIO->EVENTS_PHYEND   = 0u;
    RADIO->EVENTS_CRCOK    = 0u;
    RADIO->EVENTS_CRCERROR = 0u;
    RADIO->EVENTS_DISABLED = 0u;

    tiku_radio_arch_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;

    for (;;) {
        if (RADIO->EVENTS_END != 0u) {
            got = 1u;
            break;
        }
        spin++;
        if ((spin & 0x3FFu) == 0u) {
            RADIO->TASKS_RSSISTART = 1u;
        }
        if ((spin & 0xFFFFu) == 0u) {
            tiku_radio_arch_hfclk_kick();
            tiku_watchdog_kick();
            if ((tiku_clock_time_t)(tiku_clock_time() - start) >= dl) {
                break;
            }
        }
    }
    if (!got) {                                  /* timeout: cancel the arm    */
        RADIO->SHORTS = 0u;
        RADIO->EVENTS_DISABLED = 0u;
        RADIO->TASKS_DISABLE = 1u;
        for (spin = 0u; spin < 200000u; spin++) {
            if (RADIO->EVENTS_DISABLED != 0u) {
                break;
            }
        }
        RADIO->TIFS = 0u;
        return 0;
    }
    /* T_IFS window (~192 us): the auto-TXEN is pending.  Commit an ACK only
     * for a CRC-OK unicast DATA frame addressed to us; otherwise abort. */
    {
        uint16_t fcf  = (uint16_t)(rx_frame[1] | ((uint16_t)rx_frame[2] << 8));
        uint8_t  ftyp = (uint8_t)(fcf & 0x07u);
        uint8_t  areq = (uint8_t)((fcf >> 5) & 1u);
        uint16_t dpan = (uint16_t)(rx_frame[4] | ((uint16_t)rx_frame[5] << 8));
        uint16_t dadr = (uint16_t)(rx_frame[6] | ((uint16_t)rx_frame[7] << 8));
        uint8_t  crcok = (RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
        uint8_t  forus = (uint8_t)((dpan == my_pan) && (dadr == my_addr));
        if (crcok && ftyp == 1u && areq && forus) {
            ack_tmpl[3] = rx_frame[3];           /* echo the seq               */
            RADIO->PACKETPTR = (uint32_t)ack_tmpl;
            RADIO->EVENTS_PHYEND = 0u;
            /* Drop DISABLED_TXEN so the ACK's own DISABLED can't re-trigger a
             * second TX (the BLE spurious-TX fix, 7b36d3f). */
            RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_READY_START_Pos) |
                            ((uint32_t)1u << RADIO_SHORTS_PHYEND_DISABLE_Pos);
            ack = 1u;
        } else {
            RADIO->SHORTS = 0u;
            RADIO->TASKS_DISABLE = 1u;           /* cancel the pending TXEN    */
        }
    }
    if (rssi != 0) {
        *rssi = (int8_t)(-(int)(RADIO->RSSISAMPLE & 0x7Fu));
    }
    /* Wait for the radio to settle (ACK TX completes, or the abort disables). */
    RADIO->EVENTS_DISABLED = 0u;
    for (spin = 0u; spin < 600000u; spin++) {
        if (RADIO->EVENTS_DISABLED != 0u) {
            break;
        }
    }
    RADIO->SHORTS = 0u;
    RADIO->TIFS = 0u;
    if (did_ack != 0) {
        *did_ack = ack;
    }
    if (RADIO->CRCSTATUS != RADIO_CRCSTATUS_CRCSTATUS_CRCOk) {
        return -1;
    }
    {
        uint8_t phr  = rx_frame[0];
        uint8_t plen = (phr >= 2u) ? (uint8_t)(phr - 2u) : 0u;
        if (plen > cap) {
            plen = cap;
        }
        if (plen != 0u) {
            memcpy(buf, &rx_frame[1], plen);
        }
        return (int)plen;
    }
}

int tiku_ieee154_arch_cca(void)
{
    uint32_t spin;
    int idle = 0;

    /* Energy-detect CCA above a fixed threshold; RXREADY_CCASTART chains the
     * measurement off the ramp so no software timing is in the loop. */
    RADIO->CCACTRL =
        ((uint32_t)RADIO_CCACTRL_CCAMODE_EdMode << RADIO_CCACTRL_CCAMODE_Pos) |
        ((uint32_t)20u << RADIO_CCACTRL_CCAEDTHRES_Pos);
    RADIO->SHORTS = ((uint32_t)1u << RADIO_SHORTS_RXREADY_CCASTART_Pos);
    RADIO->EVENTS_CCAIDLE  = 0u;
    RADIO->EVENTS_CCABUSY  = 0u;
    RADIO->EVENTS_DISABLED = 0u;

    tiku_radio_arch_hfclk_kick();
    RADIO->TASKS_RXEN = 1u;
    for (spin = 0u; spin < 2000000u; spin++) {
        if (RADIO->EVENTS_CCAIDLE != 0u) {
            idle = 1;
            break;
        }
        if (RADIO->EVENTS_CCABUSY != 0u) {
            break;
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
    return idle;
}
