/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_host.c - M33-side ATT/GATT (NUS) server (Phase B).  Ported off the
 * FLPR's fll_att_handle: same fixed NUS GATT DB, now driven by L2CAP frames
 * the controller forwards over the mailbox instead of parsed on the VPR.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/bluetooth/tiku_ble_host.h>

/* NUS GATT DB (must match the client's fixed handles / a phone's discovery):
 *   0x0010 Primary Service (0x2800) = NUS service UUID
 *   0x0011 Characteristic (0x2803)  = [Write|WriteNoRsp][0x0012][RX UUID]
 *   0x0012 NUS RX value             (write target)
 *   0x0013 Characteristic (0x2803)  = [Notify][0x0014][TX UUID]
 *   0x0014 NUS TX value             (notify source)
 *   0x0015 CCCD (0x2902)                                                    */
#define HOST_H_RX    0x0012u
#define HOST_H_TX    0x0014u
#define HOST_H_CCCD  0x0015u
#define HOST_NUS_RXBUF  32u

/* NUS 128-bit UUID base, little-endian on air; byte[12] selects service/RX/TX
 * (0x01/0x02/0x03) -- 6E400001-B5A3-F393-E0A9-E50E24DCCA9E. */
static const uint8_t host_nus_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu
};

static uint8_t  host_sub;                       /* CCCD: TX notifications on */
static uint8_t  host_rx[HOST_NUS_RXBUF];        /* buffered NUS RX bytes     */
static uint8_t  host_rx_len;                    /* >0 = a write is pending   */

void tiku_ble_host_reset(void)
{
    host_sub = 0u;
    host_rx_len = 0u;
}

int tiku_ble_host_subscribed(void)
{
    return (host_sub != 0u) ? 1 : 0;
}

/* Wrap an ATT PDU in an L2CAP frame ([len][CID=0x0004][att]) into out. */
static uint16_t host_wrap(uint8_t *out, uint16_t out_cap,
                          const uint8_t *att, uint8_t alen)
{
    uint8_t i;
    if ((uint16_t)(alen + 4u) > out_cap) {
        return 0u;
    }
    out[0] = alen; out[1] = 0u;                 /* L2CAP length              */
    out[2] = 0x04u; out[3] = 0x00u;             /* CID = ATT                 */
    for (i = 0u; i < alen; i++) {
        out[4u + i] = att[i];
    }
    return (uint16_t)(4u + alen);
}

/* Emit a 128-bit NUS UUID (variant = byte[12]) into out. */
static void host_uuid(uint8_t *out, uint8_t variant)
{
    uint8_t i;
    for (i = 0u; i < 16u; i++) {
        out[i] = host_nus_uuid[i];
    }
    out[12] = variant;
}

/* ATT Error Response wrapped as an L2CAP frame. */
static uint16_t host_error(uint8_t req_op, uint16_t h, uint8_t err,
                           uint8_t *out, uint16_t out_cap)
{
    uint8_t e[5];
    e[0] = 0x01u; e[1] = req_op;
    e[2] = (uint8_t)h; e[3] = (uint8_t)(h >> 8);
    e[4] = err;
    return host_wrap(out, out_cap, e, 5u);
}

uint16_t tiku_ble_host_rx(const uint8_t *l2cap, uint16_t len,
                          uint8_t *out, uint16_t out_cap)
{
    const uint8_t *att;
    uint8_t alen, op;

    if (len < 5u) {
        return 0u;                              /* need L2CAP header + 1 ATT */
    }
    if (l2cap[2] != 0x04u || l2cap[3] != 0x00u) {
        return 0u;                              /* not ATT (CID 0x0004)      */
    }
    att = &l2cap[4];
    alen = (uint8_t)(len - 4u);
    op = att[0];

    if (op == 0x02u) {                          /* Exchange MTU Req          */
        uint8_t m[3] = { 0x03u, 23u, 0u };
        return host_wrap(out, out_cap, m, 3u);
    } else if (op == 0x10u && alen >= 7u) {     /* Read By Group Type Req    */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
        if (type == 0x2800u && start <= 0x0010u) {  /* Primary Service       */
            uint8_t r[24];
            r[0] = 0x11u; r[1] = 20u;           /* opcode, per-entry length  */
            r[2] = 0x10u; r[3] = 0x00u;         /* group start handle        */
            r[4] = 0x15u; r[5] = 0x00u;         /* group end handle          */
            host_uuid(&r[6], 0x01u);            /* NUS service UUID          */
            return host_wrap(out, out_cap, r, 22u);
        }
        return host_error(0x10u, start, 0x0Au, out, out_cap);
    } else if (op == 0x08u && alen >= 7u) {     /* Read By Type Req          */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint16_t type  = (uint16_t)(att[5] | ((uint16_t)att[6] << 8));
        if (type == 0x2803u && start <= 0x0011u) {  /* Characteristic (RX)   */
            uint8_t r[24];
            r[0] = 0x09u; r[1] = 21u;
            r[2] = 0x11u; r[3] = 0x00u;         /* declaration handle        */
            r[4] = 0x0Cu;                       /* props Write|WriteNoRsp    */
            r[5] = 0x12u; r[6] = 0x00u;         /* value handle              */
            host_uuid(&r[7], 0x02u);            /* RX char UUID              */
            return host_wrap(out, out_cap, r, 23u);
        } else if (type == 0x2803u && start <= 0x0013u) { /* char TX         */
            uint8_t r[24];
            r[0] = 0x09u; r[1] = 21u;
            r[2] = 0x13u; r[3] = 0x00u;
            r[4] = 0x10u;                       /* props Notify              */
            r[5] = 0x14u; r[6] = 0x00u;
            host_uuid(&r[7], 0x03u);            /* TX char UUID              */
            return host_wrap(out, out_cap, r, 23u);
        }
        return host_error(0x08u, start, 0x0Au, out, out_cap);
    } else if (op == 0x04u && alen >= 5u) {     /* Find Information Req       */
        uint16_t start = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        if (start <= 0x0015u && start >= 0x0014u) { /* CCCD after TX char    */
            uint8_t r[6];
            r[0] = 0x05u; r[1] = 0x01u;         /* Find Info Rsp, 16-bit fmt */
            r[2] = 0x15u; r[3] = 0x00u;         /* CCCD handle               */
            r[4] = 0x02u; r[5] = 0x29u;         /* UUID 0x2902               */
            return host_wrap(out, out_cap, r, 6u);
        }
        return host_error(0x04u, start, 0x0Au, out, out_cap);
    } else if (op == 0x12u && alen >= 4u) {     /* Write Request             */
        uint16_t h = (uint16_t)(att[1] | ((uint16_t)att[2] << 8));
        uint8_t rsp = 0x13u;                    /* Write Response            */
        if (h == HOST_H_CCCD) {
            host_sub = att[3];
        } else if (h == HOST_H_RX) {            /* client -> server bytes    */
            uint8_t n = (uint8_t)(alen - 3u), i;
            if (n > HOST_NUS_RXBUF) {
                n = HOST_NUS_RXBUF;
            }
            for (i = 0u; i < n; i++) {
                host_rx[i] = att[3u + i];
            }
            host_rx_len = n;
        }
        return host_wrap(out, out_cap, &rsp, 1u);
    }
    return 0u;                                  /* unhandled: no response    */
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
    host_rx_len = 0u;                           /* consumed                  */
    return n;
}

uint16_t tiku_ble_host_nus_notify(const uint8_t *data, uint16_t len,
                                  uint8_t *out, uint16_t out_cap)
{
    uint8_t nb[3 + 20], n, i;
    if (host_sub == 0u || len == 0u) {
        return 0u;
    }
    n = (len > 20u) ? 20u : (uint8_t)len;
    nb[0] = 0x1Bu;                              /* Handle Value Notification */
    nb[1] = (uint8_t)HOST_H_TX;
    nb[2] = (uint8_t)(HOST_H_TX >> 8);
    for (i = 0u; i < n; i++) {
        nb[3u + i] = data[i];
    }
    return host_wrap(out, out_cap, nb, (uint8_t)(3u + n));
}
