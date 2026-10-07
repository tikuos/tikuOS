/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flpr_hci.c - the FLPR as the host stack's HCI controller.
 *
 * Commands are answered on the M33, connectable advertising and the link
 * are the FLPR's job, and ACL rides the mailbox's L2CAP fragments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"                                /* the tick for the clock */
#include <arch/nordic/tiku_flpr_arch.h>
#include <arch/nordic/tiku_radio_arch.h>
#include <arch/nordic/tiku_device_select.h>      /* NRF_RADIO_S (TIFS)    */
#include <interfaces/bluetooth/tiku_bt.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <interfaces/bluetooth/tiku_ble_adv.h>   /* the radio's owner     */
#include <kernel/timers/tiku_clock.h>            /* the scan's roll       */

#include <string.h>

/* The controller has no public address (the host sets a random static
 * one), the peripheral role only, and no LL encryption: the FLPR refuses
 * LL_ENC_REQ.  Beacons stay with the broadcast facade on the M33's radio,
 * and a scan is that facade's observer. */

/* HCI packet types, and the events this controller raises. */
#define HCI_CMD                 0x01u
#define HCI_ACL                 0x02u
#define HCI_EVT                 0x04u
#define EVT_DISCONN_COMPLETE    0x05u
#define EVT_CMD_COMPLETE        0x0Eu
#define EVT_CMD_STATUS          0x0Fu
#define EVT_NUM_COMPLETED       0x13u
#define EVT_LE_META             0x3Eu
#define LE_CONN_COMPLETE        0x01u
#define LE_ADV_REPORT           0x02u

/* HCI status codes the commands answer with. */
#define ST_OK                   0x00u
#define ST_UNKNOWN_CMD          0x01u
#define ST_UNKNOWN_CONN         0x02u
#define ST_DISALLOWED           0x0Cu
#define ST_UNSUPPORTED          0x11u
#define ST_INVALID_PARAMS       0x12u
#define ST_UNSPECIFIED          0x1Fu

/* The one link's handle, and the host's ACL buffers: a data PDU's 27
 * octets each, held here until the mailbox takes them. */
#define HCI_HANDLE              0x0000u
#define ACL_LEN                 27u
#define ACL_BUFS                4u

/* What waits for the host's recv(), whole: events and received ACL. */
#define RXQ_SLOTS               8u
#define RXQ_SLOT_LEN            48u

/* The version this controller reports, as its LL_VERSION_IND does: Core
 * 5.3 (0x0C), company 0x0059, subversion 1. */
#define LL_VERSION              0x0Cu
#define LL_COMPANY              0x0059u
#define LL_SUBVERSION           0x0001u

static uint8_t  s_rxq[RXQ_SLOTS][RXQ_SLOT_LEN];
static uint8_t  s_rxq_len[RXQ_SLOTS];
static uint8_t  s_rxq_head, s_rxq_count;

static uint8_t  s_txq[ACL_BUFS][ACL_LEN];
static uint8_t  s_txq_len[ACL_BUFS];
static uint8_t  s_txq_llid[ACL_BUFS];
static uint8_t  s_txq_head, s_txq_count;

static uint8_t  s_up;                   /* registered, the FLPR running   */
static uint8_t  s_addr[6];              /* the host's random address, LE  */
/* Advertising as the host set it, and what runs on the FLPR. */
static uint8_t  s_adv_type;             /* LE Set Adv Params' type        */
static uint8_t  s_own_type;             /* ... and own address: 1 random  */
static uint8_t  s_adv_data[31];
static uint8_t  s_adv_len;
static uint8_t  s_rsp_data[31];
static uint8_t  s_rsp_len;
static uint8_t  s_adv_on;               /* the host has advertising on    */
static uint8_t  s_claimed;              /* the FLPR holds the radio       */
static uint8_t  s_linked;               /* LE Connection Complete sent    */
static uint8_t  s_scanning;             /* the observer runs for the host */
static uint8_t  s_scan_sent;            /* its table's entries reported   */
static uint16_t s_scan_named;           /* ... with their names, by index */
static tiku_clock_time_t s_scan_roll;   /* when the observer starts again */

/* The observer's table keeps the first devices it hears, so a scan starts
 * it again this often: a busy room's first dozen cannot keep the rest out,
 * and the host's own cache keeps the loudest. */
#define SCAN_ROLL               (TIKU_CLOCK_SECOND * 2u)

/*---------------------------------------------------------------------------*/
/* TO THE HOST                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Queue one whole packet for recv(); dropped when the queue is
 *         full, which only a host that stopped reading can cause. */
static void
rxq_push(const uint8_t *p, uint8_t len)
{
    uint8_t at;

    if (s_rxq_count >= RXQ_SLOTS || len > RXQ_SLOT_LEN) {
        return;
    }
    at = (uint8_t)((s_rxq_head + s_rxq_count) % RXQ_SLOTS);
    memcpy(s_rxq[at], p, len);
    s_rxq_len[at] = len;
    s_rxq_count++;
}

/** @brief Command Complete for @p op, with return parameters @p ret. */
static void
cmd_complete(uint16_t op, const uint8_t *ret, uint8_t n)
{
    uint8_t e[RXQ_SLOT_LEN];

    e[0] = HCI_EVT;
    e[1] = EVT_CMD_COMPLETE;
    e[2] = (uint8_t)(3u + n);
    e[3] = 1u;                              /* commands the host may send */
    e[4] = (uint8_t)(op & 0xFFu);
    e[5] = (uint8_t)(op >> 8);
    memcpy(&e[6], ret, n);
    rxq_push(e, (uint8_t)(6u + n));
}

/** @brief Command Complete carrying only @p status. */
static void
cmd_done(uint16_t op, uint8_t status)
{
    cmd_complete(op, &status, 1u);
}

/** @brief Command Status for @p op. */
static void
cmd_status(uint16_t op, uint8_t status)
{
    uint8_t e[7];

    e[0] = HCI_EVT;
    e[1] = EVT_CMD_STATUS;
    e[2] = 4u;
    e[3] = status;
    e[4] = 1u;
    e[5] = (uint8_t)(op & 0xFFu);
    e[6] = (uint8_t)(op >> 8);
    rxq_push(e, 7u);
}

/** @brief Number Of Completed Packets: @p n of the link's ACL sent on. */
static void
acl_completed(uint8_t n)
{
    uint8_t e[8];

    e[0] = HCI_EVT;
    e[1] = EVT_NUM_COMPLETED;
    e[2] = 5u;
    e[3] = 1u;                              /* one handle */
    e[4] = (uint8_t)(HCI_HANDLE & 0xFFu);
    e[5] = (uint8_t)(HCI_HANDLE >> 8);
    e[6] = n;
    e[7] = 0u;
    rxq_push(e, 8u);
}

/** @brief LE Connection Complete for the central the FLPR now holds. */
static void
link_up_event(void)
{
    tiku_flpr_conn_params_t cp;
    uint8_t e[22], inita[6], adva[6], types, i;

    types = tiku_flpr_arch_conn_addrs(inita, adva);
    tiku_flpr_arch_conn_params(&cp);
    e[0] = HCI_EVT;
    e[1] = EVT_LE_META;
    e[2] = 19u;
    e[3] = LE_CONN_COMPLETE;
    e[4] = ST_OK;
    e[5] = (uint8_t)(HCI_HANDLE & 0xFFu);
    e[6] = (uint8_t)(HCI_HANDLE >> 8);
    e[7] = 0x01u;                           /* role: peripheral */
    e[8] = (uint8_t)(types & 1u);           /* the central's address type */
    for (i = 0u; i < 6u; i++) {
        e[9u + i] = inita[i];
    }
    e[15] = (uint8_t)(cp.interval & 0xFFu);
    e[16] = (uint8_t)(cp.interval >> 8);
    e[17] = (uint8_t)(cp.latency & 0xFFu);
    e[18] = (uint8_t)(cp.latency >> 8);
    e[19] = (uint8_t)(cp.timeout & 0xFFu);
    e[20] = (uint8_t)(cp.timeout >> 8);
    e[21] = cp.sca;
    rxq_push(e, 22u);
}

/** @brief Disconnection Complete, with the FLPR's reason. */
static void
link_down_event(uint8_t reason)
{
    uint8_t e[7];

    e[0] = HCI_EVT;
    e[1] = EVT_DISCONN_COMPLETE;
    e[2] = 4u;
    e[3] = ST_OK;
    e[4] = (uint8_t)(HCI_HANDLE & 0xFFu);
    e[5] = (uint8_t)(HCI_HANDLE >> 8);
    e[6] = (reason != 0u) ? reason : 0x08u;
    rxq_push(e, 7u);
}

/**
 * @brief LE Advertising Report for one device the observer heard: its
 *        name as the AD data, the only field the observer keeps, and an
 *        address taken as random when its top bits say random static.
 */
static void
scan_report(const tiku_ble_adv_report_t *r)
{
    /* PDU type -> HCI event type: ADV_IND, ADV_DIRECT_IND, ADV_NONCONN_IND,
     * -, SCAN_RSP, -, ADV_SCAN_IND. */
    static const uint8_t kind[7] = { 0x00u, 0x01u, 0x03u, 0x00u, 0x04u,
                                     0x00u, 0x02u };
    uint8_t e[RXQ_SLOT_LEN], nl = (uint8_t)strlen(r->name), at;

    if (nl > (uint8_t)(RXQ_SLOT_LEN - 18u)) {
        nl = (uint8_t)(RXQ_SLOT_LEN - 18u);
    }
    e[0] = HCI_EVT;
    e[1] = EVT_LE_META;
    e[3] = LE_ADV_REPORT;
    e[4] = 1u;                              /* one report */
    e[5] = (r->adv_type < 7u) ? kind[r->adv_type] : 0x00u;
    e[6] = ((r->addr[5] & 0xC0u) == 0xC0u) ? 1u : 0u;
    memcpy(&e[7], r->addr, 6u);             /* e[7..12] */
    at = 13u;
    if (nl != 0u) {
        e[at++] = (uint8_t)(2u + nl);       /* AD data length */
        e[at++] = (uint8_t)(1u + nl);
        e[at++] = 0x09u;                    /* Complete Local Name */
        memcpy(&e[at], r->name, nl);
        at = (uint8_t)(at + nl);
    } else {
        e[at++] = 0u;
    }
    e[at++] = (uint8_t)r->rssi;
    e[2] = (uint8_t)(at - 3u);
    rxq_push(e, at);
}

/** @brief Report what the observer added or named since the last call, as
 *         far as the host's queue has room; past SCAN_ROLL, start it over. */
static void
scan_follow(void)
{
    tiku_ble_adv_report_t r;
    uint8_t i;

    if (!s_scanning) {
        return;
    }
    for (i = 0u; i < 16u && s_rxq_count < RXQ_SLOTS &&
                 tiku_ble_adv_observe_get(i, &r); i++) {
        uint16_t bit = (uint16_t)(1u << i);

        if (i >= s_scan_sent ||
            (r.name[0] != '\0' && (s_scan_named & bit) == 0u)) {
            scan_report(&r);
            if (i >= s_scan_sent) {
                s_scan_sent = (uint8_t)(i + 1u);
            }
            if (r.name[0] != '\0') {
                s_scan_named |= bit;
            }
        }
    }
    if (TIKU_CLOCK_LT(s_scan_roll, tiku_clock_time())) {
        tiku_ble_adv_observe_stop();
        if (tiku_ble_adv_observe_start(0u) != 0) {
            s_scanning = 0u;                /* the radio was taken */
            return;
        }
        s_scan_sent = 0u;
        s_scan_named = 0u;
        s_scan_roll = (tiku_clock_time_t)(tiku_clock_time() + SCAN_ROLL);
    }
}

/** @brief LE Set Scan Enable: the observer, until the host stops it. */
static uint8_t
scan_enable(uint8_t on)
{
    if (!on) {
        if (s_scanning) {
            tiku_ble_adv_observe_stop();
            s_scanning = 0u;
        }
        return ST_OK;
    }
    if (s_scanning) {
        return ST_OK;
    }
    if (tiku_ble_adv_observe_start(0u) != 0) {
        return ST_DISALLOWED;               /* the radio is someone's */
    }
    s_scanning = 1u;
    s_scan_sent = 0u;
    s_scan_named = 0u;
    s_scan_roll = (tiku_clock_time_t)(tiku_clock_time() + SCAN_ROLL);
    return ST_OK;
}

/*---------------------------------------------------------------------------*/
/* THE FLPR'S JOB                                                            */
/*---------------------------------------------------------------------------*/

/** @brief Give the radio back to the M33: the FLPR stopped, the arbiter
 *         released. */
static void
radio_release(void)
{
    if (s_claimed) {
        tiku_flpr_arch_conn_stop();           /* RADIO back to secure */
        tiku_radio_arch_constlat_hold(0);
        tiku_ble_adv_conn_release();
        s_claimed = 0u;
    }
}

/**
 * @brief Hand the FLPR the host's connectable advertising: an ADV_IND and
 *        its SCAN_RSP from the random address the host set (TxAdd 1).
 * @return ST_OK, or the HCI status saying why it did not start
 */
static uint8_t
adv_start(void)
{
    uint8_t adv[48], rsp[48], advlen, rsplen;

    if (s_claimed) {
        return ST_OK;                         /* already the FLPR's */
    }
    if (tiku_ble_adv_conn_claim() != 0) {
        return ST_DISALLOWED;                 /* a beacon or scan has it */
    }
    s_claimed = 1u;
    advlen = tiku_radio_arch_adv_build(adv, s_addr, s_adv_data, s_adv_len);
    adv[0] = (uint8_t)(s_own_type ? 0x40u : 0x00u);   /* ADV_IND, TxAdd */
    rsplen = tiku_radio_arch_scanrsp_build(rsp, s_addr, s_rsp_data,
                                           s_rsp_len);
    rsp[0] = (uint8_t)(s_own_type ? 0x44u : 0x04u);   /* SCAN_RSP */
    tiku_radio_arch_init();                   /* static link config */
    NRF_RADIO_S->TIFS = 150u;
    tiku_radio_arch_constlat_hold(1);
    if (tiku_flpr_arch_conn_start(adv, advlen, rsp, rsplen, s_addr) != 0) {
        tiku_radio_arch_constlat_hold(0);
        tiku_ble_adv_conn_release();
        s_claimed = 0u;
        return ST_UNSPECIFIED;
    }
    return ST_OK;
}

/** @brief The rest of the host's ACL, dropped with the link it was for. */
static void
txq_clear(void)
{
    s_txq_head = 0u;
    s_txq_count = 0u;
}

/**
 * @brief Follow the FLPR's job: a central taken is LE Connection Complete,
 *        the link's end Disconnection Complete, and an advertise that gave
 *        up starts again while the host still advertises.
 */
static void
link_follow(void)
{
    uint32_t st;

    if (!s_claimed) {
        return;
    }
    st = tiku_flpr_arch_conn_state();
    if (st == 1u && !s_linked) {
        s_linked = 1u;
        s_adv_on = 0u;                        /* a connection ends ADV_IND */
        link_up_event();
    } else if (st == 2u || st == 3u) {
        if (s_linked) {
            s_linked = 0u;
            txq_clear();
            link_down_event(tiku_flpr_arch_conn_reason());
        }
        radio_release();
        if (s_adv_on) {
            (void)adv_start();
        }
    }
}

/** @brief Pass the host's ACL to the mailbox while it takes a fragment,
 *         counting each back to the host as completed. */
static void
acl_drain(void)
{
    uint8_t done = 0u;

    while (s_txq_count > 0u && s_linked) {
        int rc = tiku_flpr_arch_conn_send(s_txq[s_txq_head],
                                          s_txq_len[s_txq_head],
                                          s_txq_llid[s_txq_head]);
        if (rc == -2) {
            break;                            /* the slot is still full */
        }
        s_txq_head = (uint8_t)((s_txq_head + 1u) % ACL_BUFS);
        s_txq_count--;
        if (rc < 0) {
            txq_clear();                      /* the link has gone */
            return;
        }
        done++;
    }
    if (done != 0u) {
        acl_completed(done);
    }
}

/*---------------------------------------------------------------------------*/
/* FROM THE HOST                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p op completes with Command Status, not Complete. */
static uint8_t
cmd_uses_status(uint16_t op)
{
    switch (op) {
    case 0x0406u:                             /* Disconnect */
    case 0x041Du:                             /* Read Remote Version */
    case 0x200Du:                             /* LE Create Connection */
    case 0x2013u:                             /* LE Connection Update */
    case 0x2016u:                             /* LE Read Remote Features */
    case 0x2019u:                             /* LE Enable Encryption */
    case 0x2025u:                             /* LE Read Local P-256 Key */
    case 0x2026u:                             /* LE Generate DHKey */
    case 0x2032u:                             /* LE Set PHY */
    case 0x205Eu:                             /* LE Generate DHKey v2 */
        return 1u;
    default:
        return 0u;
    }
}

/** @brief LE Set Advertising Enable: start or stop the FLPR's job. */
static uint8_t
adv_enable(uint8_t on)
{
    if (!on) {
        s_adv_on = 0u;
        if (!s_linked) {
            radio_release();
        }
        return ST_OK;
    }
    if (s_adv_type != 0x00u || !s_own_type) {
        return ST_UNSUPPORTED;                /* ADV_IND, random address */
    }
    if (s_linked) {
        return ST_DISALLOWED;                 /* one link at a time */
    }
    s_adv_on = 1u;
    if (adv_start() != ST_OK) {
        s_adv_on = 0u;
        return ST_DISALLOWED;
    }
    return ST_OK;
}

/** @brief Answer one HCI command: @p p is its parameters, @p n their
 *         length. */
static void
command(uint16_t op, const uint8_t *p, uint8_t n)
{
    uint8_t r[12];

    switch (op) {
    case 0x0C03u:                             /* Reset */
        s_adv_on = 0u;
        s_linked = 0u;
        txq_clear();
        radio_release();
        (void)scan_enable(0u);
        cmd_done(op, ST_OK);
        return;
    case 0x200Bu:                             /* LE Set Scan Parameters */
        cmd_done(op, s_scanning ? ST_DISALLOWED : ST_OK);
        return;
    case 0x200Cu:                             /* LE Set Scan Enable */
        cmd_done(op, (n >= 1u) ? scan_enable(p[0]) : ST_INVALID_PARAMS);
        return;
    case 0x0C01u:                             /* Set Event Mask */
    case 0x2001u:                             /* LE Set Event Mask */
        cmd_done(op, ST_OK);
        return;
    case 0x1001u:                             /* Read Local Version */
        r[0] = ST_OK;
        r[1] = LL_VERSION;
        r[2] = (uint8_t)(LL_SUBVERSION & 0xFFu);
        r[3] = (uint8_t)(LL_SUBVERSION >> 8);
        r[4] = LL_VERSION;
        r[5] = (uint8_t)(LL_COMPANY & 0xFFu);
        r[6] = (uint8_t)(LL_COMPANY >> 8);
        r[7] = (uint8_t)(LL_SUBVERSION & 0xFFu);
        r[8] = (uint8_t)(LL_SUBVERSION >> 8);
        cmd_complete(op, r, 9u);
        return;
    case 0x1009u:                             /* Read BD_ADDR */
        memset(r, 0, 7u);                     /* none: the host sets a */
        cmd_complete(op, r, 7u);              /* random static address */
        return;
    case 0x2005u:                             /* LE Set Random Address */
        if (n < 6u || (p[5] & 0xC0u) != 0xC0u) {
            cmd_done(op, ST_INVALID_PARAMS);  /* static addresses only */
        } else if (s_adv_on) {
            cmd_done(op, ST_DISALLOWED);
        } else {
            memcpy(s_addr, p, 6u);
            cmd_done(op, ST_OK);
        }
        return;
    case 0x1005u:                             /* Read Buffer Size */
        r[0] = ST_OK;
        r[1] = ACL_LEN;
        r[2] = 0u;
        r[3] = 0u;                            /* no SCO */
        r[4] = ACL_BUFS;
        r[5] = 0u;
        r[6] = 0u;
        r[7] = 0u;
        cmd_complete(op, r, 8u);
        return;
    case 0x2002u:                             /* LE Read Buffer Size */
        r[0] = ST_OK;
        r[1] = ACL_LEN;
        r[2] = 0u;
        r[3] = ACL_BUFS;
        cmd_complete(op, r, 4u);
        return;
    case 0x2006u:                             /* LE Set Adv Parameters */
        if (n < 15u) {
            cmd_done(op, ST_INVALID_PARAMS);
        } else if (s_adv_on) {
            cmd_done(op, ST_DISALLOWED);
        } else {
            s_adv_type = p[4];                /* interval: the FLPR's own */
            s_own_type = (uint8_t)(p[5] & 1u);
            cmd_done(op, ST_OK);
        }
        return;
    case 0x2007u:                             /* LE Read Adv TX Power */
        r[0] = ST_OK;
        r[1] = (uint8_t)tiku_radio_arch_txpower();
        cmd_complete(op, r, 2u);
        return;
    case 0x2008u:                             /* LE Set Advertising Data */
    case 0x2009u:                             /* LE Set Scan Response Data */
        if (n < 1u || p[0] > 31u || n < (uint8_t)(1u + p[0])) {
            cmd_done(op, ST_INVALID_PARAMS);
            return;
        }
        if (op == 0x2008u) {
            memcpy(s_adv_data, &p[1], p[0]);
            s_adv_len = p[0];
        } else {
            memcpy(s_rsp_data, &p[1], p[0]);
            s_rsp_len = p[0];
        }
        cmd_done(op, ST_OK);
        return;
    case 0x200Au:                             /* LE Set Advertising Enable */
        cmd_done(op, (n >= 1u) ? adv_enable(p[0]) : ST_INVALID_PARAMS);
        return;
    case 0x0406u:                             /* Disconnect */
        if (n < 3u || !s_linked ||
            (uint16_t)(p[0] | ((uint16_t)(p[1] & 0x0Fu) << 8)) !=
                HCI_HANDLE) {
            cmd_status(op, ST_UNKNOWN_CONN);
            return;
        }
        cmd_status(op, ST_OK);
        tiku_flpr_arch_conn_terminate(p[2]);  /* its end is reported */
        return;
    default:
        if (cmd_uses_status(op)) {
            cmd_status(op, ST_UNKNOWN_CMD);
        } else {
            cmd_done(op, ST_UNKNOWN_CMD);
        }
        return;
    }
}

/** @brief Queue one host ACL packet for the mailbox.  @return 0, or -1 for
 *         a packet the buffers offered cannot hold. */
static int
acl_from_host(const uint8_t *pkt, uint16_t len)
{
    uint16_t hdr = (uint16_t)(pkt[1] | ((uint16_t)pkt[2] << 8));
    uint16_t dlen = (uint16_t)(pkt[3] | ((uint16_t)pkt[4] << 8));
    uint8_t  pb = (uint8_t)((hdr >> 12) & 0x03u);
    uint8_t  at;

    if ((hdr & 0x0FFFu) != HCI_HANDLE || dlen > ACL_LEN ||
        (uint16_t)(5u + dlen) > len || s_txq_count >= ACL_BUFS ||
        !s_linked) {
        return -1;
    }
    at = (uint8_t)((s_txq_head + s_txq_count) % ACL_BUFS);
    memcpy(s_txq[at], &pkt[5], dlen);
    s_txq_len[at] = (uint8_t)dlen;
    s_txq_llid[at] = (pb == 0x01u) ? 1u : 2u;   /* continuation : start */
    s_txq_count++;
    acl_drain();
    return 0;
}

/*---------------------------------------------------------------------------*/
/* THE TRANSPORT                                                             */
/*---------------------------------------------------------------------------*/

/** @brief tiku_bt_transport_t send: a command answered here, or ACL. */
static int
hci_send(const uint8_t *pkt, uint16_t len)
{
    if (!s_up) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    if (pkt == (const uint8_t *)0 || len < 4u) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (pkt[0] == HCI_CMD && len >= (uint16_t)(4u + pkt[3])) {
        command((uint16_t)(pkt[1] | ((uint16_t)pkt[2] << 8)), &pkt[4],
                pkt[3]);
        return TIKU_DRV_OK;
    }
    if (pkt[0] == HCI_ACL && len >= 5u) {
        return (acl_from_host(pkt, len) == 0) ? TIKU_DRV_OK
                                              : TIKU_DRV_ERR_INVALID;
    }
    return TIKU_DRV_ERR_INVALID;
}

/**
 * @brief tiku_bt_transport_t recv: a queued event first, else the next
 *        fragment the FLPR received, as ACL.
 */
static int
hci_recv(uint8_t *out, uint16_t out_max)
{
    uint8_t llid;
    int     n;

    if (out == (uint8_t *)0 || out_max < 5u) {
        return TIKU_DRV_ERR_INVALID;
    }
    if (!s_up) {
        return 0;
    }
    link_follow();
    acl_drain();
    scan_follow();
    if (s_rxq_count > 0u) {
        uint8_t len = s_rxq_len[s_rxq_head];

        if (len > out_max) {
            return TIKU_DRV_ERR_INVALID;      /* stays for a larger buffer */
        }
        memcpy(out, s_rxq[s_rxq_head], len);
        s_rxq_head = (uint8_t)((s_rxq_head + 1u) % RXQ_SLOTS);
        s_rxq_count--;
        return (int)len;
    }
    if (!s_linked || !tiku_flpr_arch_conn_rx_ready()) {
        return 0;
    }
    n = tiku_flpr_arch_conn_recv(&out[5], (uint32_t)(out_max - 5u), &llid);
    if (n <= 0) {
        return 0;
    }
    out[0] = HCI_ACL;
    out[1] = (uint8_t)(HCI_HANDLE & 0xFFu);
    out[2] = (uint8_t)(((HCI_HANDLE >> 8) & 0x0Fu) |
                       ((llid == 2u) ? 0x20u : 0x10u));   /* PB 10 : 01 */
    out[3] = (uint8_t)n;
    out[4] = 0u;
    return 5 + n;
}

static int
hci_ready(void)
{
    return s_up;
}

static const char *
hci_version(void)
{
    return "FLPR";
}

static const tiku_bt_transport_t flpr_hci_transport = {
    .send     = hci_send,
    .recv     = hci_recv,
    .is_ready = hci_ready,
    .wait     = (void (*)(uint16_t))0,
    .version  = hci_version,
};

int
tiku_bt_controller_power(uint8_t on)
{
    uint8_t addr[6];
    int rc;

    if (!on) {
        if (s_up) {
            tiku_bt_shutdown();
            (void)scan_enable(0u);
            s_adv_on = 0u;
            s_linked = 0u;
            txq_clear();
            radio_release();
            s_rxq_count = 0u;
            s_up = 0u;
        }
        return TIKU_DRV_OK;
    }
    if (s_up) {
        return TIKU_DRV_OK;
    }
    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        return TIKU_DRV_ERR_NOT_PRESENT;
    }
    memset(s_addr, 0, sizeof s_addr);
    s_rxq_head = 0u;
    s_rxq_count = 0u;
    txq_clear();
    s_up = 1u;
    (void)tiku_bt_register_transport(&flpr_hci_transport);
    rc = tiku_bt_init();
    if (rc == TIKU_DRV_OK && tiku_bt_addr(addr) != TIKU_DRV_OK) {
        rc = TIKU_DRV_ERR_TIMEOUT;
    }
    if (rc != TIKU_DRV_OK) {
        (void)tiku_bt_controller_power(0u);
    }
    return rc;
}
