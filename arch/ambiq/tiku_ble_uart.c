/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_uart.c - Minimal connectable GATT peripheral (BLE UART service)
 *
 * A tiny hand-rolled BLE host on top of the EM9305 SPI-HCI transport
 * (tiku_em9305): connectable advertising, a polled HCI event/ACL pump, LE
 * connection handling, an L2CAP-LE + ATT server, and the BLE UART service.
 * This is the wireless-shell transport (M3). No Cordio, no AmbiqSuite.
 *
 * Bring-up is staged so each layer has its own HW gate:
 *   M3.1  connectable adv + connection/disconnection detection   <-- this file
 *   M3.2  ATT server + BLE UART discovery
 *   M3.3  RX/TX wired to the shell io-backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_ble_uart.h"

#include <arch/ambiq/tiku_em9305.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 *  HCI constants
 * ------------------------------------------------------------------ */

/* Command opcodes (mirrors the private set in tiku_em9305.c). */
#define HCI_OP_RESET               0x0C03u
#define HCI_OP_LE_SET_EVENT_MASK   0x2001u
#define HCI_OP_LE_SET_ADV_PARAM    0x2006u
#define HCI_OP_LE_SET_ADV_DATA     0x2008u
#define HCI_OP_LE_SET_ADV_ENABLE   0x200Au
#define HCI_OP_DISCONNECT          0x0406u

/* HCI packet type bytes (first byte of every framed packet). */
#define HCI_PKT_ACL                0x02u
#define HCI_PKT_EVENT              0x04u

/* HCI event codes. */
#define HCI_EVT_DISCONN_COMPLETE   0x05u
#define HCI_EVT_NUM_COMPLETE       0x13u   /* Number Of Completed Packets      */
#define HCI_EVT_LE_META            0x3Eu

/* LE Meta subevent codes. A controller reports a new link as either the legacy
 * Connection Complete (0x01) or the Enhanced Connection Complete (0x0A); the two
 * share identical leading fields (status, handle, role, peer address), so the
 * same parse handles both -- we just have to accept both subevent codes. */
#define HCI_LE_CONN_COMPLETE       0x01u
#define HCI_LE_ENH_CONN_COMPLETE   0x0Au

/* Advertising types. */
#define ADV_IND                    0x00u   /* connectable undirected           */

#define CONN_HANDLE_NONE           0xFFFFu

/* ---- L2CAP / ATT / GATT (BLE UART service) ---- */

#define L2CAP_CID_ATT              0x0004u
#define L2CAP_CID_LE_SIG           0x0005u   /* LE signaling channel           */

/* GATT declaration UUIDs (16-bit). */
#define GATT_PRIMARY_SVC           0x2800u
#define GATT_CHARACTERISTIC        0x2803u
#define GATT_CCCD                  0x2902u

/* ATT opcodes. */
#define ATT_ERROR_RSP              0x01u
#define ATT_EXCHANGE_MTU_REQ       0x02u
#define ATT_EXCHANGE_MTU_RSP       0x03u
#define ATT_FIND_INFO_REQ          0x04u
#define ATT_FIND_INFO_RSP          0x05u
#define ATT_READ_BY_TYPE_REQ       0x08u
#define ATT_READ_BY_TYPE_RSP       0x09u
#define ATT_READ_REQ               0x0Au
#define ATT_READ_RSP               0x0Bu
#define ATT_READ_BY_GRP_REQ        0x10u
#define ATT_READ_BY_GRP_RSP        0x11u
#define ATT_WRITE_REQ              0x12u
#define ATT_WRITE_RSP              0x13u
#define ATT_HANDLE_VALUE_NTF       0x1Bu
#define ATT_WRITE_CMD              0x52u

/* ATT error codes. */
#define ATT_ERR_INVALID_HANDLE     0x01u
#define ATT_ERR_READ_NOT_PERM      0x02u
#define ATT_ERR_WRITE_NOT_PERM     0x03u
#define ATT_ERR_REQ_NOT_SUPP       0x06u
#define ATT_ERR_ATTR_NOT_FOUND     0x0Au

/* BLE UART attribute handles (fixed layout). */
#define ATT_H_SVC                  0x0001u   /* primary service declaration     */
#define ATT_H_RX_DECL              0x0002u   /* RX characteristic declaration   */
#define ATT_H_RX_VAL               0x0003u   /* RX value (phone -> device write) */
#define ATT_H_TX_DECL              0x0004u   /* TX characteristic declaration   */
#define ATT_H_TX_VAL               0x0005u   /* TX value (device -> phone notify) */
#define ATT_H_TX_CCCD              0x0006u   /* TX client config descriptor     */
#define ATT_H_LAST                 0x0006u   /* last handle in the service      */

/* Characteristic properties. */
#define CHAR_PROP_WRITE_NR         0x04u     /* write without response          */
#define CHAR_PROP_WRITE            0x08u     /* write                           */
#define CHAR_PROP_NOTIFY           0x10u     /* notify                          */

/* ATT MTU we offer. Notifications carry up to MTU-3 bytes of shell output. */
#define BLEUART_SERVER_MTU             247u

/* ------------------------------------------------------------------ *
 *  State
 * ------------------------------------------------------------------ */

static tiku_ble_uart_conn_t s_conn = { CONN_HANDLE_NONE, 0u, 0u, {0,0,0,0,0,0} };
static uint8_t             s_started;

/* ATT connection state. */
static uint16_t            s_att_mtu = 23u;   /* negotiated ATT MTU (23 default) */
static uint16_t            s_tx_cccd;         /* TX CCCD value (bit0 = notify on) */

/* Controller TX flow control: the EM9305 holds a handful of ACL buffers and
 * SILENTLY DROPS a packet sent with none free. Track packets in flight against
 * the controller-reported budget (HCI LE Read Buffer Size) and gate every
 * notification on a free credit; Number-Of-Completed-Packets returns them. */
static int                 s_acl_inflight;    /* ACL packets sent, not yet acked */
static int                 s_acl_credits = 2; /* controller ACL buffer count     */
static uint16_t            s_acl_pkt_len = 27u; /* controller max ACL data bytes */

/* Live LL TX payload limit, from the LE Data Length Change event. A link opens
 * at the 27-octet LL default, and the EM9305 DROPS (rather than fragments) an
 * ACL bigger than this -- so every outbound packet is sized to it. The central
 * typically raises it to ~251 moments after connecting. */
static uint16_t            s_ll_tx_octets = 27u;

/* Inbound HCI reassembly stream. The radio's SPI side is a byte STREAM: one
 * framed read may carry a partial packet (large ACL writes span frames) or
 * several whole packets back-to-back (coalesced completed-packet events --
 * losing one of those permanently leaks a TX credit). Frames append here and
 * complete packets are peeled off by their HCI header length. */
static uint8_t             s_stream[560];
static uint16_t            s_stream_len;

/* Defined with the ATT server below; used by the connection-setup path too. */
static int bleuart_l2cap_send(uint16_t cid, const uint8_t *pdu, uint16_t len);

/* RX ring: bytes the phone wrote to the RX characteristic, drained by the
 * shell as console input. TX buffer: shell output, flushed as TX notifications. */
#define BLEUART_RX_RING        256u
#define BLEUART_TX_BUF         1024u
static uint8_t             s_rx_ring[BLEUART_RX_RING];
static uint16_t            s_rx_head, s_rx_tail;
static uint8_t             s_tx_buf[BLEUART_TX_BUF];
static uint16_t            s_tx_len;

static void bleuart_rx_push(const uint8_t *d, uint16_t n) {
    uint16_t i;
    for (i = 0u; i < n; i++) {
        uint16_t nxt = (uint16_t)((s_rx_head + 1u) % BLEUART_RX_RING);
        if (nxt == s_rx_tail) {
            break;                          /* ring full -- drop the rest       */
        }
        s_rx_ring[s_rx_head] = d[i];
        s_rx_head = nxt;
    }
}

/* 128-bit base UUID 6E400001-B5A3-F393-E0A9-E50E24DCCA9E in little-endian wire
 * order. Byte [12] selects the member: 01 = service, 02 = RX, 03 = TX. These
 * are deliberately the well-known Nordic UART Service UUIDs: keeping them lets
 * stock BLE-serial apps (nRF Connect, etc.) and generic clients interoperate
 * with this hand-rolled server unchanged -- no vendor code, only the UUIDs. */
static const uint8_t       BLEUART_UUID_BASE[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu
};

/* Raw bytes of the most recent LE Meta event, kept for bring-up diagnostics
 * (lets the shell confirm which connection-complete subevent the radio sends). */
static uint8_t             s_last_meta[24];
static uint8_t             s_last_meta_len;

/* Trace ring of the last few raw packets the pump read (type + head bytes),
 * so a session dump shows exactly what the controller delivered. */
#define BLEUART_TRACE_N        8u
#define BLEUART_TRACE_CAP      18u
static struct { uint8_t len; uint8_t b[BLEUART_TRACE_CAP]; } s_trace[BLEUART_TRACE_N];
static uint8_t             s_trace_head;   /* next slot to write */
static uint8_t             s_trace_count;  /* valid entries (<= BLEUART_TRACE_N) */

static void bleuart_trace(const uint8_t *p, uint16_t len) {
    uint8_t n = (len < BLEUART_TRACE_CAP) ? (uint8_t)len : (uint8_t)BLEUART_TRACE_CAP;
    memcpy(s_trace[s_trace_head].b, p, n);
    s_trace[s_trace_head].len = n;
    s_trace_head = (uint8_t)((s_trace_head + 1u) % BLEUART_TRACE_N);
    if (s_trace_count < BLEUART_TRACE_N) {
        s_trace_count++;
    }
}

/* Scratch buffer for one framed HCI packet (event or ACL). The EM9305 hands
 * back whole packets bounded by its STS2 space byte; 255 covers HCI events and
 * a single ATT MTU worth of ACL. */
static uint8_t             s_pkt[260];

/* Per-step diagnostics for the last tiku_ble_uart_start(): result code + status
 * byte of each setup HCI command. Exposed for the shell so a failed bring-up
 * says exactly which command timed out. */
#define BLEUART_MAX_STEPS      6u
static int8_t              s_dbg_rc[BLEUART_MAX_STEPS];
static uint8_t             s_dbg_st[BLEUART_MAX_STEPS];
static uint8_t             s_dbg_nsteps;

/*
 * Drain any HCI packets the controller already has waiting (RDY asserted).
 *
 * The EM9305 signals "I have something to send" by raising RDY; if we start a
 * host *write* while a *read* is pending the framed transaction collides and
 * the command is lost. An HCI host must service events between commands, so
 * clear the pipe (non-blocking: timeout 0 reads only if RDY is already high)
 * before issuing the next command.
 */
static void bleuart_drain(void) {
    uint8_t  tmp[64];
    uint16_t l;
    int      guard = 8;
    while (guard-- > 0 &&
           tiku_em9305_recv(tmp, sizeof(tmp), &l, 0u) == TIKU_EM9305_OK) {
        /* discard */
    }
}

/* Pull every SPI frame the radio has pending (RDY high) into the reassembly
 * stream. Bounded, non-blocking. Also called before each host WRITE so a
 * pending inbound frame can never collide with (and lose) the write. */
static void bleuart_slurp(void) {
    int guard = 16;
    while (guard-- > 0) {
        uint16_t room = (uint16_t)(sizeof(s_stream) - s_stream_len);
        uint16_t l = 0u;
        if (room == 0u) {
            break;                          /* stream full -- extract first    */
        }
        if (tiku_em9305_recv(s_stream + s_stream_len, room, &l, 0u)
            != TIKU_EM9305_OK) {
            break;                          /* nothing (more) pending          */
        }
        s_stream_len = (uint16_t)(s_stream_len + l);
    }
}

/*
 * Peel one complete HCI packet off the front of the stream into @p out.
 * Packet length comes from the HCI header: event = 3 + len byte, ACL = 5 +
 * 16-bit data length. Returns the packet length, or 0 if the stream holds
 * only a partial packet (more frames needed).
 */
static uint16_t bleuart_stream_extract(uint8_t *out, uint16_t cap) {
    for (;;) {
        uint16_t need;

        if (s_stream_len == 0u) {
            return 0u;
        }
        /* Resync guard: a stream must start with an event (0x04) or ACL
         * (0x02) type byte; anything else is desync -- shed a byte and retry. */
        if (s_stream[0] != 0x04u && s_stream[0] != 0x02u) {
            memmove(s_stream, s_stream + 1, (uint16_t)(s_stream_len - 1u));
            s_stream_len--;
            continue;
        }
        if (s_stream[0] == 0x04u) {
            if (s_stream_len < 3u) {
                return 0u;                  /* header incomplete               */
            }
            need = (uint16_t)(3u + s_stream[2]);
        } else {
            if (s_stream_len < 5u) {
                return 0u;
            }
            need = (uint16_t)(5u + (s_stream[3] | ((uint16_t)s_stream[4] << 8)));
        }
        if (need > (uint16_t)sizeof(s_stream)) {
            s_stream_len = 0u;              /* impossible length: hard resync  */
            return 0u;
        }
        if (s_stream_len < need) {
            return 0u;                      /* wait for the rest               */
        }
        if (need > cap) {
            need = cap;                     /* truncate into caller's buffer   */
        }
        memcpy(out, s_stream, need);
        {
            uint16_t full = (s_stream[0] == 0x04u)
                                ? (uint16_t)(3u + s_stream[2])
                                : (uint16_t)(5u + (s_stream[3] |
                                                   ((uint16_t)s_stream[4] << 8)));
            memmove(s_stream, s_stream + full,
                    (uint16_t)(s_stream_len - full));
            s_stream_len = (uint16_t)(s_stream_len - full);
        }
        return need;
    }
}

/* ------------------------------------------------------------------ *
 *  Advertising setup
 * ------------------------------------------------------------------ */

/* Service pending events, issue one setup command, and record diagnostics.
 * @p i is the step index (0..3). Returns the hci_cmd result. */
static int bleuart_step(uint8_t i, uint16_t op, const uint8_t *p, uint8_t plen,
                    uint8_t *bad) {
    uint8_t st = 0xFFu;
    int     rc;

    bleuart_drain();
    rc = tiku_em9305_hci_cmd(op, p, plen, &st);
    if (i < BLEUART_MAX_STEPS) {
        s_dbg_rc[i] = (int8_t)rc;
        s_dbg_st[i] = st;
        if ((uint8_t)(i + 1u) > s_dbg_nsteps) {
            s_dbg_nsteps = (uint8_t)(i + 1u);
        }
    }
    if (rc == TIKU_EM9305_OK) {
        *bad |= st;
    }
    return rc;
}

/*
 * Build + program the LE advertising set as a CONNECTABLE peripheral named
 * @p name, then enable advertising. Mirrors tiku_em9305_beacon() but with
 * ADV_IND (0x00) so a central may connect. Statuses OR'd into *bad (0 = ok).
 */
static int bleuart_advertise(const char *name, uint8_t *bad) {
    static const uint8_t adv_params[15] = {
        0xA0u, 0x00u,          /* min interval 160 * 0.625ms = 100 ms          */
        0xA0u, 0x00u,          /* max interval 100 ms                          */
        ADV_IND,               /* connectable undirected                       */
        0x00u,                 /* own address type: public                     */
        0x00u,                 /* peer address type                            */
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,  /* peer address (unused)    */
        0x07u,                 /* channel map 37/38/39                         */
        0x00u                  /* filter policy: allow any                     */
    };
    static uint8_t adv_data[32];
    uint8_t nlen, idx, en;

    /* Enable the LE meta events we depend on. The controller gates LE events
     * (Connection Complete, etc.) behind this mask; on the EM9305 a bare HCI
     * Reset leaves them off, so without this the host never learns a central
     * connected even though the link (and ATT) is live. Enable bits 0..15. */
    static const uint8_t le_evt_mask[8] = {
        0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    if (name == NULL) {
        name = "tikuOS";
    }
    *bad = 0u;
    s_dbg_nsteps = 0u;

    /* HCI Reset -> known state. */
    if (bleuart_step(0u, HCI_OP_RESET, NULL, 0u, bad) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    /* LE Set Event Mask -> deliver Connection Complete & friends. */
    if (bleuart_step(1u, HCI_OP_LE_SET_EVENT_MASK, le_evt_mask,
                 (uint8_t)sizeof(le_evt_mask), bad) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    /* Advertising parameters (connectable). */
    if (bleuart_step(2u, HCI_OP_LE_SET_ADV_PARAM, adv_params,
                 (uint8_t)sizeof(adv_params), bad) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    /* Advertising data: [sig-len][Flags AD][Complete Local Name AD]. */
    memset(adv_data, 0, sizeof(adv_data));
    idx = 1u;
    adv_data[idx++] = 0x02u;               /* Flags AD: len                    */
    adv_data[idx++] = 0x01u;               /*           type = Flags           */
    adv_data[idx++] = 0x06u;               /* LE General Disc + no BR/EDR      */
    nlen = (uint8_t)strlen(name);
    if (nlen > 26u) {
        nlen = 26u;                        /* keep within the 31-byte AD budget */
    }
    adv_data[idx++] = (uint8_t)(nlen + 1u);/* Name AD: len                     */
    adv_data[idx++] = 0x09u;               /*          type = Complete Local Name */
    memcpy(adv_data + idx, name, nlen);
    idx = (uint8_t)(idx + nlen);
    adv_data[0] = (uint8_t)(idx - 1u);     /* significant byte count           */
    if (bleuart_step(3u, HCI_OP_LE_SET_ADV_DATA, adv_data,
                 (uint8_t)sizeof(adv_data), bad) != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    /* Enable advertising. */
    en = 0x01u;
    if (bleuart_step(4u, HCI_OP_LE_SET_ADV_ENABLE, &en, 1u, bad)
        != TIKU_EM9305_OK) {
        return TIKU_EM9305_ERR_TIMEOUT;
    }

    return TIKU_EM9305_OK;
}

/* Toggle advertising on/off. The adv set (params + data) persists in the
 * controller across connections, so re-advertising after a disconnect is just
 * a re-enable. */
static int bleuart_adv_enable(uint8_t on) {
    uint8_t st;
    return tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_ENABLE, &on, 1u, &st);
}

/*
 * HCI LE Read Buffer Size (0x2002): learn the controller's ACL data budget --
 * max bytes per ACL packet and how many it can hold. Those two numbers drive
 * the TX flow control (credits) and the notification chunk size; the defaults
 * (27/2) are the spec minimums and stay if the query fails.
 */
static void bleuart_read_buffer_size(void) {
    static const uint8_t cmd[4] = { 0x01u, 0x02u, 0x20u, 0x00u };
    uint8_t  ev[16];
    uint16_t el = 0u;

    bleuart_drain();
    if (tiku_em9305_send(cmd, (uint16_t)sizeof(cmd)) != TIKU_EM9305_OK) {
        return;
    }
    if (tiku_em9305_recv(ev, sizeof(ev), &el, 500u) != TIKU_EM9305_OK) {
        return;
    }
    /* Command Complete: 04 0E len 01 02 20 status pkt_len_lo pkt_len_hi num */
    if (el >= 10u && ev[0] == 0x04u && ev[1] == 0x0Eu &&
        ev[4] == 0x02u && ev[5] == 0x20u && ev[6] == 0x00u) {
        uint16_t l = (uint16_t)(ev[7] | ((uint16_t)ev[8] << 8));
        if (l >= 27u) {
            s_acl_pkt_len = l;
        }
        if (ev[9] > 0u) {
            s_acl_credits = (int)ev[9];
        }
    }
}

int tiku_ble_uart_start(const char *name) {
    uint8_t  bad = 0u;
    uint8_t  bev[16];
    uint16_t bl = 0u;
    int      rc;

    s_conn.handle = CONN_HANDLE_NONE;
    s_started = 0u;
    s_att_mtu = 23u;
    s_tx_cccd = 0u;
    s_rx_head = s_rx_tail = 0u;
    s_tx_len = 0u;
    s_acl_inflight = 0;
    s_stream_len = 0u;
    s_ll_tx_octets = 27u;

    /* Bring the radio up (EN pulse + boot event), then drain the boot event. */
    rc = tiku_em9305_reset();
    if (rc != TIKU_EM9305_OK) {
        return rc;
    }
    (void)tiku_em9305_recv(bev, sizeof(bev), &bl, 500u);   /* {04 FF 01 01} */

    rc = bleuart_advertise(name, &bad);
    if (rc != TIKU_EM9305_OK) {
        return rc;
    }
    if (bad != 0u) {
        return TIKU_EM9305_ERR_NOTREADY;
    }
    bleuart_read_buffer_size();       /* ACL packet size + credit budget for TX */
    s_started = 1u;
    return TIKU_EM9305_OK;
}

/* ------------------------------------------------------------------ *
 *  L2CAP-LE + ATT server (BLE UART service)
 * ------------------------------------------------------------------ */

/* Write a BLE UART member UUID (01 = service, 02 = RX, 03 = TX) into @p out[16]. */
static void bleuart_uuid(uint8_t *out, uint8_t member) {
    memcpy(out, BLEUART_UUID_BASE, 16);
    out[12] = member;
}

/* Send one L2CAP PDU on @p cid to the peer as an HCI ACL packet. Returns 0 on
 * success (the controller accepted it -- one TX credit consumed), negative if
 * the write could not be delivered. */
static int bleuart_l2cap_send(uint16_t cid, const uint8_t *pdu, uint16_t len) {
    static uint8_t out[9u + BLEUART_SERVER_MTU];
    uint16_t acl;
    int rc;

    if (s_conn.handle == CONN_HANDLE_NONE) {
        return -1;
    }
    if (len > BLEUART_SERVER_MTU) {
        len = BLEUART_SERVER_MTU;
    }
    acl = (uint16_t)(4u + len);            /* L2CAP header + payload           */
    out[0] = HCI_PKT_ACL;
    out[1] = (uint8_t)(s_conn.handle & 0xFFu);
    out[2] = (uint8_t)((s_conn.handle >> 8) & 0x0Fu);  /* PB=00 (start) BC=00  */
    out[3] = (uint8_t)(acl & 0xFFu);
    out[4] = (uint8_t)(acl >> 8);
    out[5] = (uint8_t)(len & 0xFFu);
    out[6] = (uint8_t)(len >> 8);
    out[7] = (uint8_t)(cid & 0xFFu);
    out[8] = (uint8_t)(cid >> 8);
    memcpy(out + 9, pdu, len);
    bleuart_slurp();                        /* pending inbound frame must not
                                         * collide with (and lose) this write */
    rc = tiku_em9305_send(out, (uint16_t)(9u + len));
    if (rc != TIKU_EM9305_OK) {
        return -1;
    }
    s_acl_inflight++;                   /* controller now holds one more packet */
    return 0;
}

/* Send one ATT PDU over the ATT fixed channel. */
static int bleuart_att_send(const uint8_t *att, uint16_t att_len) {
    return bleuart_l2cap_send(L2CAP_CID_ATT, att, att_len);
}

/* Send an ATT Error Response for @p req_op on @p handle with code @p err. */
static void bleuart_att_error(uint8_t req_op, uint16_t handle, uint8_t err) {
    uint8_t r[5];
    r[0] = ATT_ERROR_RSP;
    r[1] = req_op;
    r[2] = (uint8_t)(handle & 0xFFu);
    r[3] = (uint8_t)(handle >> 8);
    r[4] = err;
    bleuart_att_send(r, 5u);
}

/* Little read helpers for request fields. */
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/*
 * Dispatch one ATT request (@p att[0..att_len-1], opcode at att[0]) and send the
 * matching response. Implements just enough of the ATT server for a phone to
 * discover the BLE UART service and enable notifications.
 *
 * @return TIKU_BLE_EVT_RX if RX data arrived (Stage 3), else TIKU_BLE_EVT_ATT.
 */
static int bleuart_att_handle(const uint8_t *att, uint16_t len) {
    uint8_t op;

    if (len < 1u) {
        return TIKU_BLE_EVT_NONE;
    }
    op = att[0];

    switch (op) {
    case ATT_EXCHANGE_MTU_REQ: {
        uint16_t cli = (len >= 3u) ? rd16(att + 1) : 23u;
        uint8_t  r[3];
        s_att_mtu = (cli < BLEUART_SERVER_MTU) ? cli : BLEUART_SERVER_MTU;
        if (s_att_mtu < 23u) {
            s_att_mtu = 23u;
        }
        r[0] = ATT_EXCHANGE_MTU_RSP;
        r[1] = (uint8_t)(BLEUART_SERVER_MTU & 0xFFu);
        r[2] = (uint8_t)(BLEUART_SERVER_MTU >> 8);
        bleuart_att_send(r, 3u);
        break;
    }

    case ATT_READ_BY_GRP_REQ: {           /* primary service discovery */
        uint16_t start, end;
        if (len < 7u) { bleuart_att_error(op, 0u, ATT_ERR_INVALID_HANDLE); break; }
        start = rd16(att + 1);
        end   = rd16(att + 3);
        /* Group type must be Primary Service (0x2800), and our service in range. */
        if (att[5] == (uint8_t)(GATT_PRIMARY_SVC & 0xFFu) &&
            att[6] == (uint8_t)(GATT_PRIMARY_SVC >> 8) &&
            start <= ATT_H_SVC && end >= ATT_H_SVC) {
            uint8_t r[2u + 4u + 16u];
            r[0] = ATT_READ_BY_GRP_RSP;
            r[1] = 4u + 16u;              /* each entry: handles(4) + UUID(16)   */
            r[2] = (uint8_t)(ATT_H_SVC & 0xFFu);  r[3] = (uint8_t)(ATT_H_SVC >> 8);
            r[4] = (uint8_t)(ATT_H_LAST & 0xFFu); r[5] = (uint8_t)(ATT_H_LAST >> 8);
            bleuart_uuid(r + 6, 0x01u);
            bleuart_att_send(r, (uint16_t)sizeof(r));
        } else {
            bleuart_att_error(op, start, ATT_ERR_ATTR_NOT_FOUND);
        }
        break;
    }

    case ATT_READ_BY_TYPE_REQ: {          /* characteristic discovery */
        uint16_t start, end;
        if (len < 7u) { bleuart_att_error(op, 0u, ATT_ERR_INVALID_HANDLE); break; }
        start = rd16(att + 1);
        end   = rd16(att + 3);
        if (att[5] == (uint8_t)(GATT_CHARACTERISTIC & 0xFFu) &&
            att[6] == (uint8_t)(GATT_CHARACTERISTIC >> 8)) {
            uint8_t  r[2u + 2u * (2u + 1u + 2u + 16u)];
            uint8_t *p = r + 2;
            uint8_t  n = 0u;
            r[0] = ATT_READ_BY_TYPE_RSP;
            r[1] = 2u + 1u + 2u + 16u;    /* handle + props + val handle + UUID */
            if (start <= ATT_H_RX_DECL && ATT_H_RX_DECL <= end) {
                p[0] = (uint8_t)(ATT_H_RX_DECL & 0xFFu);
                p[1] = (uint8_t)(ATT_H_RX_DECL >> 8);
                p[2] = (uint8_t)(CHAR_PROP_WRITE | CHAR_PROP_WRITE_NR);
                p[3] = (uint8_t)(ATT_H_RX_VAL & 0xFFu);
                p[4] = (uint8_t)(ATT_H_RX_VAL >> 8);
                bleuart_uuid(p + 5, 0x02u);
                p += 21; n++;
            }
            if (start <= ATT_H_TX_DECL && ATT_H_TX_DECL <= end) {
                p[0] = (uint8_t)(ATT_H_TX_DECL & 0xFFu);
                p[1] = (uint8_t)(ATT_H_TX_DECL >> 8);
                p[2] = CHAR_PROP_NOTIFY;
                p[3] = (uint8_t)(ATT_H_TX_VAL & 0xFFu);
                p[4] = (uint8_t)(ATT_H_TX_VAL >> 8);
                bleuart_uuid(p + 5, 0x03u);
                p += 21; n++;
            }
            if (n > 0u) {
                bleuart_att_send(r, (uint16_t)(2u + (uint16_t)n * 21u));
            } else {
                bleuart_att_error(op, start, ATT_ERR_ATTR_NOT_FOUND);
            }
        } else {
            bleuart_att_error(op, (len >= 3u) ? rd16(att + 1) : 0u,
                          ATT_ERR_ATTR_NOT_FOUND);
        }
        break;
    }

    case ATT_FIND_INFO_REQ: {             /* descriptor discovery (CCCD) */
        uint16_t start, end;
        if (len < 5u) { bleuart_att_error(op, 0u, ATT_ERR_INVALID_HANDLE); break; }
        start = rd16(att + 1);
        end   = rd16(att + 3);
        if (start <= ATT_H_TX_CCCD && ATT_H_TX_CCCD <= end) {
            uint8_t r[6];
            r[0] = ATT_FIND_INFO_RSP;
            r[1] = 0x01u;                 /* handles + 16-bit UUIDs             */
            r[2] = (uint8_t)(ATT_H_TX_CCCD & 0xFFu);
            r[3] = (uint8_t)(ATT_H_TX_CCCD >> 8);
            r[4] = (uint8_t)(GATT_CCCD & 0xFFu);
            r[5] = (uint8_t)(GATT_CCCD >> 8);
            bleuart_att_send(r, 6u);
        } else {
            bleuart_att_error(op, start, ATT_ERR_ATTR_NOT_FOUND);
        }
        break;
    }

    case ATT_READ_REQ: {                  /* read the CCCD */
        uint16_t h;
        if (len < 3u) { bleuart_att_error(op, 0u, ATT_ERR_INVALID_HANDLE); break; }
        h = rd16(att + 1);
        if (h == ATT_H_TX_CCCD) {
            uint8_t r[3];
            r[0] = ATT_READ_RSP;
            r[1] = (uint8_t)(s_tx_cccd & 0xFFu);
            r[2] = (uint8_t)(s_tx_cccd >> 8);
            bleuart_att_send(r, 3u);
        } else {
            bleuart_att_error(op, h, ATT_ERR_READ_NOT_PERM);
        }
        break;
    }

    case ATT_WRITE_REQ:
    case ATT_WRITE_CMD: {
        uint16_t h;
        int rxed = 0;
        if (len < 3u) { bleuart_att_error(op, 0u, ATT_ERR_INVALID_HANDLE); break; }
        h = rd16(att + 1);
        if (h == ATT_H_TX_CCCD) {
            s_tx_cccd = (len >= 4u) ? rd16(att + 3)
                                    : (uint16_t)(len >= 3u ? att[3] : 0u);
        } else if (h == ATT_H_RX_VAL) {
            if (len > 3u) {
                bleuart_rx_push(att + 3, (uint16_t)(len - 3u));   /* -> shell input */
            }
            rxed = 1;
        } else if (op == ATT_WRITE_REQ) {
            bleuart_att_error(op, h, ATT_ERR_WRITE_NOT_PERM);
            break;
        }
        if (op == ATT_WRITE_REQ) {
            uint8_t r = ATT_WRITE_RSP;
            bleuart_att_send(&r, 1u);
        }
        if (rxed) {
            return TIKU_BLE_EVT_RX;
        }
        break;
    }

    default:
        bleuart_att_error(op, 0u, ATT_ERR_REQ_NOT_SUPP);
        break;
    }

    return TIKU_BLE_EVT_ATT;
}

/* ------------------------------------------------------------------ *
 *  Event / ACL pump
 * ------------------------------------------------------------------ */

/* Handle one HCI *event* packet (s_pkt[0] == 0x04). */
static int bleuart_on_event(const uint8_t *ev, uint16_t len) {
    if (len < 2u) {
        return TIKU_BLE_EVT_NONE;
    }

    /* Number Of Completed Packets: 04 13 len num_h [handle:2 count:2]*.
     * Each acks packets the controller finished transmitting, freeing its ACL
     * buffers -- the credit our TX flow control waits on. */
    if (ev[1] == HCI_EVT_NUM_COMPLETE && len >= 4u) {
        uint8_t nh = ev[3];
        uint8_t i;
        for (i = 0u; i < nh; i++) {
            uint16_t off = (uint16_t)(4u + (uint16_t)i * 4u + 2u);   /* count */
            if ((uint16_t)(off + 1u) < len) {
                int cnt = (int)(ev[off] | ((uint16_t)ev[off + 1u] << 8));
                s_acl_inflight -= cnt;
                if (s_acl_inflight < 0) {
                    s_acl_inflight = 0;
                }
            }
        }
        return TIKU_BLE_EVT_NONE;
    }

    /* Disconnection Complete: 04 05 04 status handle_lo handle_hi reason.
     * The controller stops advertising when a link forms, so re-enable it here
     * to stay connectable for the next central. */
    if (ev[1] == HCI_EVT_DISCONN_COMPLETE && len >= 6u) {
        s_conn.handle = CONN_HANDLE_NONE;
        s_att_mtu = 23u;
        s_tx_cccd = 0u;
        s_rx_head = s_rx_tail = 0u;
        s_tx_len = 0u;
        s_acl_inflight = 0;
        s_stream_len = 0u;
        s_ll_tx_octets = 27u;
        if (s_started) {
            (void)bleuart_adv_enable(1u);
        }
        return TIKU_BLE_EVT_DISCONNECTED;
    }

    /* LE Meta -> (Enhanced) Connection Complete:
     * 04 3E len <sub> status hh_lo hh_hi role peer_type peer[6] ... */
    if (ev[1] == HCI_EVT_LE_META && len >= 4u) {
        uint8_t n = (len < (uint16_t)sizeof(s_last_meta))
                        ? (uint8_t)len : (uint8_t)sizeof(s_last_meta);
        memcpy(s_last_meta, ev, n);      /* snapshot for diagnostics */
        s_last_meta_len = n;

        /* Data Length Change: 04 3E len 07 hh hh tx_octets(2) tx_time(2)...
         * -- the new LL TX payload budget every outbound ACL must fit. */
        if (ev[3] == 0x07u && len >= 8u) {
            uint16_t tx = (uint16_t)(ev[6] | ((uint16_t)ev[7] << 8));
            if (tx >= 27u) {
                s_ll_tx_octets = tx;
            }
            return TIKU_BLE_EVT_NONE;
        }

        if ((ev[3] == HCI_LE_CONN_COMPLETE ||
             ev[3] == HCI_LE_ENH_CONN_COMPLETE) &&
            len >= 15u && ev[4] == 0x00u) {          /* status 0 = success */
            s_conn.handle    = (uint16_t)(ev[5] | ((uint16_t)ev[6] << 8));
            s_conn.handle   &= 0x0FFFu;
            s_conn.role      = ev[7];
            s_conn.peer_type = ev[8];
            memcpy(s_conn.peer, ev + 9, 6);
            return TIKU_BLE_EVT_CONNECTED;
        }
    }

    return TIKU_BLE_EVT_NONE;
}

uint8_t tiku_ble_uart_trace(uint8_t i, uint8_t *buf, uint8_t cap) {
    uint8_t start, idx, n;
    if (i >= s_trace_count) {
        return 0u;
    }
    /* oldest-first: entries live at (head - count + i) mod N */
    start = (uint8_t)((s_trace_head + BLEUART_TRACE_N - s_trace_count) % BLEUART_TRACE_N);
    idx = (uint8_t)((start + i) % BLEUART_TRACE_N);
    n = (s_trace[idx].len < cap) ? s_trace[idx].len : cap;
    if (buf && n) {
        memcpy(buf, s_trace[idx].b, n);
    }
    return n;
}

uint8_t tiku_ble_uart_trace_count(void) {
    return s_trace_count;
}

uint8_t tiku_ble_uart_last_meta(uint8_t *buf, uint8_t cap) {
    uint8_t n = (s_last_meta_len < cap) ? s_last_meta_len : cap;
    if (buf && n) {
        memcpy(buf, s_last_meta, n);
    }
    return n;
}

/*
 * Handle one HCI ACL data packet (s_pkt[0] == 0x02): unwrap L2CAP and, for the
 * ATT fixed channel (CID 0x0004), dispatch the ATT request.
 *
 * Layout: [0]=type [1..2]=handle|flags [3..4]=ACL len [5..6]=L2CAP len
 *         [7..8]=CID [9..]=ATT PDU.
 *
 * Also serves as the connection fallback: if ATT data is flowing but we never
 * saw a Connection Complete event, adopt the link from the ACL handle.
 */
static int bleuart_on_acl(const uint8_t *acl, uint16_t len) {
    uint16_t cid, l2len, attlen;
    int      adopted = TIKU_BLE_EVT_NONE;

    if (len < 5u) {
        return TIKU_BLE_EVT_NONE;
    }
    if (s_conn.handle == CONN_HANDLE_NONE) {
        s_conn.handle = (uint16_t)(acl[1] | ((uint16_t)acl[2] << 8)) & 0x0FFFu;
        adopted = TIKU_BLE_EVT_CONNECTED;
    }
    if (len < 9u) {
        return adopted;
    }
    l2len = (uint16_t)(acl[5] | ((uint16_t)acl[6] << 8));
    cid   = (uint16_t)(acl[7] | ((uint16_t)acl[8] << 8));
    if (cid != L2CAP_CID_ATT) {
        return adopted;                    /* not ATT -- ignore (signalling etc.) */
    }
    attlen = (uint16_t)(len - 9u);
    if (l2len < attlen) {
        attlen = l2len;                    /* trust the L2CAP length field       */
    }
    {
        int r = bleuart_att_handle(acl + 9, attlen);
        /* A freshly adopted link reports CONNECTED even though we also just
         * served (e.g.) the MTU request in the same packet. */
        return (adopted == TIKU_BLE_EVT_CONNECTED) ? TIKU_BLE_EVT_CONNECTED : r;
    }
}

int tiku_ble_uart_poll(void) {
    uint16_t len;

    /* Non-blocking: append whatever frames the radio has pending, then peel
     * exactly one complete HCI packet off the stream and dispatch it. Partial
     * packets stay buffered; extra coalesced packets are served next poll. */
    bleuart_slurp();
    len = bleuart_stream_extract(s_pkt, (uint16_t)sizeof(s_pkt));
    if (len == 0u) {
        return TIKU_BLE_EVT_NONE;
    }
    bleuart_trace(s_pkt, len);

    switch (s_pkt[0]) {
    case HCI_PKT_EVENT:
        return bleuart_on_event(s_pkt, len);
    case HCI_PKT_ACL:
        return bleuart_on_acl(s_pkt, len);
    default:
        return TIKU_BLE_EVT_NONE;
    }
}

uint8_t tiku_ble_uart_start_steps(int8_t *rc, uint8_t *st, uint8_t cap) {
    uint8_t i;
    uint8_t n = (s_dbg_nsteps < cap) ? s_dbg_nsteps : cap;
    for (i = 0u; i < n; i++) {
        rc[i] = s_dbg_rc[i];
        st[i] = s_dbg_st[i];
    }
    return s_dbg_nsteps;
}

int tiku_ble_uart_connected(void) {
    return (s_conn.handle != CONN_HANDLE_NONE);
}

int tiku_ble_uart_notify_enabled(void) {
    return (s_tx_cccd & 0x0001u) != 0u;
}

/* --- shell io-backend hooks: RX -> console in, console out -> TX --- */

int tiku_ble_uart_getc(void) {
    int c;
    if (s_rx_tail == s_rx_head) {
        return -1;
    }
    c = (int)s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % BLEUART_RX_RING);
    return c;
}

uint8_t tiku_ble_uart_rx_ready(void) {
    return (uint8_t)(s_rx_head != s_rx_tail);
}

void tiku_ble_uart_flush(void) {
    static uint8_t att[3u + BLEUART_SERVER_MTU];
    uint16_t n, lim;

    if (s_tx_len == 0u) {
        return;
    }
    if (s_conn.handle == CONN_HANDLE_NONE || !(s_tx_cccd & 0x0001u)) {
        s_tx_len = 0u;                      /* no subscriber -- drop            */
        return;
    }
    if (s_acl_inflight >= s_acl_credits) {
        return;                             /* no TX credit -- caller retries   */
    }

    /* One notification: bounded by the ATT MTU, the controller's ACL buffer,
     * and (hardest) the LIVE LL TX payload -- the EM9305 drops, not fragments,
     * an ACL bigger than that (7 = 4 L2CAP + 3 notification header). */
    lim = (uint16_t)(s_att_mtu - 3u);
    if (s_acl_pkt_len > 7u && lim > (uint16_t)(s_acl_pkt_len - 7u)) {
        lim = (uint16_t)(s_acl_pkt_len - 7u);
    }
    if (s_ll_tx_octets > 7u && lim > (uint16_t)(s_ll_tx_octets - 7u)) {
        lim = (uint16_t)(s_ll_tx_octets - 7u);
    }
    n = (s_tx_len < lim) ? s_tx_len : lim;

    att[0] = ATT_HANDLE_VALUE_NTF;
    att[1] = (uint8_t)(ATT_H_TX_VAL & 0xFFu);
    att[2] = (uint8_t)(ATT_H_TX_VAL >> 8);
    memcpy(att + 3, s_tx_buf, n);
    if (bleuart_att_send(att, (uint16_t)(3u + n)) != 0) {
        return;                             /* keep the bytes; retry later      */
    }
    if (n < s_tx_len) {
        memmove(s_tx_buf, s_tx_buf + n, (uint16_t)(s_tx_len - n));
        s_tx_len = (uint16_t)(s_tx_len - n);
    } else {
        s_tx_len = 0u;
    }
}

void tiku_ble_uart_putc(char c) {
    /* A full buffer self-drains: pump the stack (acks return TX credits) and
     * flush until space opens. Bounded so a dead link cannot wedge the shell;
     * on timeout the byte is dropped rather than blocking forever. */
    if (s_tx_len >= (uint16_t)sizeof(s_tx_buf)) {
        uint32_t spins = 2000000u;
        while (s_tx_len >= (uint16_t)sizeof(s_tx_buf) && spins-- > 0u) {
            (void)tiku_ble_uart_poll();
            tiku_ble_uart_flush();
        }
        if (s_tx_len >= (uint16_t)sizeof(s_tx_buf)) {
            return;
        }
    }
    s_tx_buf[s_tx_len++] = (uint8_t)c;
}

uint16_t tiku_ble_uart_tx_pending(void) {
    return s_tx_len;
}

uint16_t tiku_ble_uart_att_mtu(void) {
    return s_att_mtu;
}

int tiku_ble_uart_tx_inflight(void) {
    return s_acl_inflight;
}

uint16_t tiku_ble_uart_acl_pkt_len(void) {
    return s_acl_pkt_len;
}

int tiku_ble_uart_acl_credits(void) {
    return s_acl_credits;
}

void tiku_ble_uart_tx_credit_reset(void) {
    s_acl_inflight = 0;
}

const tiku_ble_uart_conn_t *tiku_ble_uart_conn_info(void) {
    return &s_conn;
}

void tiku_ble_uart_stop(void) {
    uint8_t st;

    /* Drop the link if one is up. Disconnect returns a Command *Status* event,
     * not Command Complete, so fire it raw (fire-and-forget) rather than via
     * tiku_em9305_hci_cmd(), which would stall waiting for a 0x0E it never gets.
     * The controller closes the link and reports a Disconnection Complete. */
    if (s_conn.handle != CONN_HANDLE_NONE) {
        uint8_t p[7];
        p[0] = 0x01u;                       /* HCI command packet             */
        p[1] = (uint8_t)(HCI_OP_DISCONNECT & 0xFFu);
        p[2] = (uint8_t)(HCI_OP_DISCONNECT >> 8);
        p[3] = 0x03u;                       /* parameter length               */
        p[4] = (uint8_t)(s_conn.handle & 0xFFu);
        p[5] = (uint8_t)(s_conn.handle >> 8);
        p[6] = 0x13u;                       /* reason: remote user terminated */
        (void)tiku_em9305_send(p, sizeof(p));
        s_conn.handle = CONN_HANDLE_NONE;
    }
    if (s_started) {
        uint8_t en = 0x00u;
        (void)tiku_em9305_hci_cmd(HCI_OP_LE_SET_ADV_ENABLE, &en, 1u, &st);
        s_started = 0u;
    }
}
