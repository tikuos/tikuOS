/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.c - M33-side L2CAP (frag/recomb) + ATT/GATT (NUS) server.
 * Phase C: recombine incoming L2CAP fragments (by LLID) into whole PDUs
 * before running ATT, and fragment responses/notifications back out, so
 * payloads can exceed one ~27-byte BLE data PDU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_host.h>

/* NUS GATT DB (fixed handles the client discovers / hard-codes):
 *   0x0010 Primary Service (0x2800) = NUS UUID
 *   0x0011 Char (0x2803) [Write|WriteNoRsp][0x0012][RX UUID]
 *   0x0012 NUS RX value (write target)
 *   0x0013 Char (0x2803) [Notify][0x0014][TX UUID]
 *   0x0014 NUS TX value (notify source)
 *   0x0015 CCCD (0x2902)                                                    */
#define HOST_H_RX     0x0012u
#define HOST_H_TX     0x0014u
#define HOST_H_CCCD   0x0015u
#define HOST_FRAG_MAX 27u                        /* L2CAP bytes per data PDU  */
#define HOST_L2_MAX   (TIKU_BLE_HOST_MTU + 4u)   /* max L2CAP PDU (MTU + hdr) */

static const uint8_t host_nus_uuid[16] = {       /* 6E400001-...-24DCCA9E    */
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu
};

static uint8_t  host_sub;                         /* CCCD: notifications on   */
static uint8_t  host_rx[TIKU_BLE_HOST_MTU];       /* buffered NUS RX bytes    */
static uint8_t  host_rx_len;
static uint8_t  host_rc[HOST_L2_MAX];             /* RX recombination buffer  */
static uint16_t host_rc_len, host_rc_expect;
static uint8_t  host_tx[HOST_L2_MAX];             /* one pending TX L2CAP PDU */
static uint16_t host_tx_len, host_tx_off;
static uint8_t  host_sig_id;                      /* L2CAP signalling ident   */
static uint8_t  host_cpu_resp;                    /* 0 none, 1 accept, 2 reject*/

void tiku_ble_host_reset(void)
{
    host_sub = 0u;
    host_rx_len = 0u;
    host_rc_len = 0u; host_rc_expect = 0u;
    host_tx_len = 0u; host_tx_off = 0u;
    host_sig_id = 0u; host_cpu_resp = 0u;
}

int tiku_ble_host_subscribed(void)
{
    return (host_sub != 0u) ? 1 : 0;
}

/* Queue a whole L2CAP PDU for (possibly fragmented) TX. */
static void host_store_tx(const uint8_t *pdu, uint16_t len)
{
    uint16_t i;
    if (len > (uint16_t)sizeof(host_tx)) {
        len = (uint16_t)sizeof(host_tx);
    }
    for (i = 0u; i < len; i++) {
        host_tx[i] = pdu[i];
    }
    host_tx_len = len;
    host_tx_off = 0u;
}

/* Wrap an ATT PDU in an L2CAP frame (CID 0x0004) and queue it for TX. */
static void host_reply(const uint8_t *att, uint8_t alen)
{
    uint8_t pdu[4 + 24];
    uint8_t i;
    if (alen > 24u) {
        alen = 24u;
    }
    pdu[0] = alen; pdu[1] = 0u;                  /* L2CAP length              */
    pdu[2] = 0x04u; pdu[3] = 0x00u;              /* CID = ATT                 */
    for (i = 0u; i < alen; i++) {
        pdu[4u + i] = att[i];
    }
    host_store_tx(pdu, (uint16_t)(4u + alen));
}

static void host_uuid(uint8_t *out, uint8_t variant)
{
    uint8_t i;
    for (i = 0u; i < 16u; i++) {
        out[i] = host_nus_uuid[i];
    }
    out[12] = variant;
}

static void host_error(uint8_t req_op, uint16_t h, uint8_t err)
{
    uint8_t e[5];
    e[0] = 0x01u; e[1] = req_op;
    e[2] = (uint8_t)h; e[3] = (uint8_t)(h >> 8);
    e[4] = err;
    host_reply(e, 5u);
}

/* ATT/GATT (NUS) server on a whole, recombined L2CAP PDU. */
static void host_att(const uint8_t *l2cap, uint16_t len)
{
    const uint8_t *att;
    uint8_t alen, op;

    if (len < 5u || l2cap[2] != 0x04u || l2cap[3] != 0x00u) {
        return;                                  /* not a 1-byte-plus ATT PDU */
    }
    att = &l2cap[4];
    alen = (uint8_t)(len - 4u);
    op = att[0];

    if (op == 0x02u) {                           /* Exchange MTU Req          */
        uint8_t m[3];
        m[0] = 0x03u; m[1] = (uint8_t)TIKU_BLE_HOST_MTU; m[2] = 0u;
        host_reply(m, 3u);
    } else if (op == 0x10u && alen >= 7u) {      /* Read By Group Type Req    */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
        if (type == 0x2800u && start <= 0x0010u) {
            uint8_t r[24];
            r[0] = 0x11u; r[1] = 20u;
            r[2] = 0x10u; r[3] = 0x00u;
            r[4] = 0x15u; r[5] = 0x00u;
            host_uuid(&r[6], 0x01u);
            host_reply(r, 22u);
        } else {
            host_error(0x10u, start, 0x0Au);
        }
    } else if (op == 0x08u && alen >= 7u) {      /* Read By Type Req          */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
        if (type == 0x2803u && start <= 0x0011u) {
            uint8_t r[24];
            r[0] = 0x09u; r[1] = 21u;
            r[2] = 0x11u; r[3] = 0x00u;
            r[4] = 0x0Cu;
            r[5] = 0x12u; r[6] = 0x00u;
            host_uuid(&r[7], 0x02u);
            host_reply(r, 23u);
        } else if (type == 0x2803u && start <= 0x0013u) {
            uint8_t r[24];
            r[0] = 0x09u; r[1] = 21u;
            r[2] = 0x13u; r[3] = 0x00u;
            r[4] = 0x10u;
            r[5] = 0x14u; r[6] = 0x00u;
            host_uuid(&r[7], 0x03u);
            host_reply(r, 23u);
        } else {
            host_error(0x08u, start, 0x0Au);
        }
    } else if (op == 0x04u && alen >= 5u) {      /* Find Information Req       */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        if (start <= 0x0015u && start >= 0x0014u) {
            uint8_t r[6];
            r[0] = 0x05u; r[1] = 0x01u;
            r[2] = 0x15u; r[3] = 0x00u;
            r[4] = 0x02u; r[5] = 0x29u;
            host_reply(r, 6u);
        } else {
            host_error(0x04u, start, 0x0Au);
        }
    } else if (op == 0x12u && alen >= 4u) {      /* Write Request             */
        uint16_t h = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint8_t rsp = 0x13u;
        if (h == HOST_H_CCCD) {
            host_sub = att[3];
        } else if (h == HOST_H_RX) {
            uint8_t n = (uint8_t)(alen - 3u), i;
            if (n > (uint8_t)sizeof(host_rx)) {
                n = (uint8_t)sizeof(host_rx);
            }
            for (i = 0u; i < n; i++) {
                host_rx[i] = att[3u + i];
            }
            host_rx_len = n;                     /* recombined NUS RX bytes   */
        }
        host_reply(&rsp, 1u);
    }
}

/* L2CAP signalling (CID 0x0005): we sent a Connection Parameter Update
 * Request; note the central's Response (accepted/rejected). */
static void host_sig(const uint8_t *l2cap, uint16_t len)
{
    const uint8_t *sig;
    if (len < 6u) {
        return;
    }
    sig = &l2cap[4];                             /* [code][id][len:2][data]   */
    if (sig[0] == 0x13u) {                       /* Conn Param Update Rsp     */
        uint16_t result = (uint16_t)(sig[4] | ((uint16_t)sig[5] << 8));
        host_cpu_resp = (result == 0x0000u) ? 1u : 2u;
    }
}

int tiku_ble_host_rx(const uint8_t *frag, uint16_t len, uint8_t llid)
{
    uint16_t i;

    if (llid == 2u) {                            /* start of a new L2CAP PDU  */
        host_rc_len = 0u;
        host_rc_expect = 0u;
        if (len >= 2u) {
            host_rc_expect = (uint16_t)(frag[0] |
                                        ((uint16_t)frag[1] << 8)) + 4u;
            if (host_rc_expect > (uint16_t)sizeof(host_rc)) {
                host_rc_expect = (uint16_t)sizeof(host_rc);
            }
        }
    }
    for (i = 0u; i < len && host_rc_len < (uint16_t)sizeof(host_rc); i++) {
        host_rc[host_rc_len++] = frag[i];
    }
    if (host_rc_expect >= 4u && host_rc_len >= host_rc_expect) {
        if (host_rc[2] == 0x05u && host_rc[3] == 0x00u) {
            host_sig(host_rc, host_rc_expect);   /* L2CAP signalling channel  */
        } else {
            host_att(host_rc, host_rc_expect);   /* ATT (CID 0x0004)          */
        }
        host_rc_len = 0u; host_rc_expect = 0u;
        return 1;
    }
    return 0;
}

uint16_t tiku_ble_host_next_tx(uint8_t *out, uint16_t out_cap, uint8_t *llid)
{
    uint16_t n, i;

    if (host_tx_off >= host_tx_len) {
        return 0u;                               /* nothing pending / done    */
    }
    n = (uint16_t)(host_tx_len - host_tx_off);
    if (n > HOST_FRAG_MAX) {
        n = HOST_FRAG_MAX;
    }
    if (n > out_cap) {
        n = out_cap;
    }
    for (i = 0u; i < n; i++) {
        out[i] = host_tx[host_tx_off + i];
    }
    if (llid != (uint8_t *)0) {
        *llid = (host_tx_off == 0u) ? 2u : 1u;   /* first vs continuation     */
    }
    host_tx_off += n;
    if (host_tx_off >= host_tx_len) {
        host_tx_len = 0u; host_tx_off = 0u;      /* PDU fully sent            */
    }
    return n;
}

uint16_t tiku_ble_host_nus_recv(uint8_t *buf, uint16_t cap)
{
    uint8_t n = host_rx_len, i;
    if (n == 0u) {
        return 0u;
    }
    if (n > cap) {
        n = (uint8_t)cap;
    }
    for (i = 0u; i < n; i++) {
        buf[i] = host_rx[i];
    }
    host_rx_len = 0u;
    return n;
}

int tiku_ble_host_nus_notify(const uint8_t *data, uint16_t len)
{
    uint8_t  pdu[HOST_L2_MAX];
    uint16_t n, i;

    if (host_sub == 0u || data == (const uint8_t *)0) {
        return -1;
    }
    if (host_tx_len != 0u) {
        return -2;                               /* a PDU is still draining   */
    }
    n = len;
    if (n > (uint16_t)(TIKU_BLE_HOST_MTU - 3u)) {
        n = (uint16_t)(TIKU_BLE_HOST_MTU - 3u);  /* ATT value <= MTU - 3      */
    }
    pdu[0] = (uint8_t)(3u + n); pdu[1] = 0u;     /* L2CAP length              */
    pdu[2] = 0x04u; pdu[3] = 0x00u;              /* CID = ATT                 */
    pdu[4] = 0x1Bu;                              /* Handle Value Notification */
    pdu[5] = (uint8_t)HOST_H_TX;
    pdu[6] = (uint8_t)(HOST_H_TX >> 8);
    for (i = 0u; i < n; i++) {
        pdu[7u + i] = data[i];
    }
    host_store_tx(pdu, (uint16_t)(7u + n));
    return 0;
}

int tiku_ble_host_request_conn_param(uint16_t interval_min,
                                     uint16_t interval_max,
                                     uint16_t latency, uint16_t timeout)
{
    uint8_t pdu[16];                             /* 4 L2CAP + 4 sig hdr + 8   */
    if (host_tx_len != 0u) {
        return -2;                               /* a PDU is still draining   */
    }
    host_cpu_resp = 0u;
    pdu[0] = 12u; pdu[1] = 0u;                   /* L2CAP length              */
    pdu[2] = 0x05u; pdu[3] = 0x00u;              /* CID = signalling          */
    pdu[4] = 0x12u;                              /* Conn Param Update Request */
    pdu[5] = ++host_sig_id;                      /* identifier                */
    pdu[6] = 0x08u; pdu[7] = 0x00u;              /* signalling data length 8  */
    pdu[8]  = (uint8_t)interval_min; pdu[9]  = (uint8_t)(interval_min >> 8);
    pdu[10] = (uint8_t)interval_max; pdu[11] = (uint8_t)(interval_max >> 8);
    pdu[12] = (uint8_t)latency;      pdu[13] = (uint8_t)(latency >> 8);
    pdu[14] = (uint8_t)timeout;      pdu[15] = (uint8_t)(timeout >> 8);
    host_store_tx(pdu, 16u);
    return 0;
}

int tiku_ble_host_conn_param_result(void)
{
    return (host_cpu_resp == 1u) ? 1 : ((host_cpu_resp == 2u) ? -1 : 0);
}
