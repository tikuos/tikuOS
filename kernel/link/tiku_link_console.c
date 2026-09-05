/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_console.c - the console backend of the link: one CRC-guarded
 * frame per message on a marked channel, a peer's paced stream answered
 * with ACKs from inside the console's pump.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_link_console.h"
#include <kernel/console/tiku_console.h>
#include <kernel/vfs/tiku_vfs.h>          /* TIKU_VFS_CAP_ALL */

#define CRC_INIT 0xFFFFu

static tiku_link_console_stats_t stats;

/*---------------------------------------------------------------------------*/
/* THE CRC                                                                   */
/*---------------------------------------------------------------------------*/

/* CRC-16/CCITT-FALSE, polynomial 0x1021, one table step per nibble. */
static const uint16_t crc_nibble[16] = {
    0x0000u, 0x1021u, 0x2042u, 0x3063u, 0x4084u, 0x50A5u, 0x60C6u, 0x70E7u,
    0x8108u, 0x9129u, 0xA14Au, 0xB16Bu, 0xC18Cu, 0xD1ADu, 0xE1CEu, 0xF1EFu
};

uint16_t
tiku_link_console_crc(uint16_t crc, const void *bytes, size_t len)
{
    const uint8_t *p = (const uint8_t *)bytes;
    size_t i;

    if (p == (const uint8_t *)0) {
        return crc;
    }
    for (i = 0; i < len; i++) {
        crc = (uint16_t)((crc << 4) ^ crc_nibble[((crc >> 12) ^ (p[i] >> 4))
                                                 & 0x0Fu]);
        crc = (uint16_t)((crc << 4) ^ crc_nibble[((crc >> 12) ^ p[i])
                                                 & 0x0Fu]);
    }
    return crc;
}

/*---------------------------------------------------------------------------*/
/* OUT                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief Answer a peer's paced frame: an ACK naming @p seq. */
static void
send_ack(const tiku_link_console_t *lc, uint8_t seq)
{
    uint8_t hdr[2], crc[2];
    uint16_t c;

    hdr[0] = TIKU_LINK_CONSOLE_ACK;
    hdr[1] = seq;
    c = tiku_link_console_crc(CRC_INIT, hdr, sizeof hdr);
    crc[0] = (uint8_t)(c >> 8);
    crc[1] = (uint8_t)c;
    (void)tiku_console_send_frame(lc->marker, hdr, sizeof hdr, crc,
                                  sizeof crc);
    stats.acked++;
}

/**
 * @brief Send one unpaced DATA frame: the header, both parts of the
 *        message straight from the caller's buffers, then the CRC.
 */
static int
console_send(tiku_link_t *l, const void *head, size_t hlen,
             const void *body, size_t blen)
{
    tiku_link_console_t *lc = (tiku_link_console_t *)l->ctx;
    uint8_t hdr[2], crc[2];
    uint16_t c;

    hdr[0] = TIKU_LINK_CONSOLE_DATA;
    hdr[1] = lc->tx_seq;
    c = tiku_link_console_crc(CRC_INIT, hdr, sizeof hdr);
    c = tiku_link_console_crc(c, head, hlen);
    c = tiku_link_console_crc(c, body, blen);
    crc[0] = (uint8_t)(c >> 8);
    crc[1] = (uint8_t)c;
    if (tiku_console_frame_begin(lc->marker) != 0) {
        return -1;
    }
    tiku_console_frame_put(hdr, sizeof hdr);
    tiku_console_frame_put(head, hlen);
    tiku_console_frame_put(body, blen);
    tiku_console_frame_put(crc, sizeof crc);
    tiku_console_frame_end();
    lc->tx_seq++;
    stats.tx++;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* IN                                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief A paced DATA frame: delivered and acknowledged when it is the one
 *        expected, acknowledged again when it is a recent repeat, dropped
 *        unanswered otherwise.
 */
static void
paced(tiku_link_console_t *lc, uint8_t seq, uint8_t *buf, size_t len)
{
    uint8_t behind;

    if (!lc->paced_synced) {
        lc->paced_expect = seq;     /* a peer that sent no SYN starts here */
        lc->paced_synced = 1u;
    }
    if (seq == lc->paced_expect) {
        tiku_link_deliver(&lc->link, buf + 2, len - TIKU_LINK_CONSOLE_OVERHEAD);
        stats.rx++;
        lc->paced_expect++;
        send_ack(lc, seq);          /* after delivery: the ACK means taken */
        return;
    }
    behind = (uint8_t)(lc->paced_expect - seq);
    if (behind >= 1u && behind <= TIKU_LINK_CONSOLE_DUP_SPAN) {
        stats.dup++;
        send_ack(lc, (uint8_t)(lc->paced_expect - 1u));
    } else {
        stats.reject++;
    }
}

/**
 * @brief The console's channel handler: check the frame, then act on its
 *        kind.  An ACK is ignored, since nothing sent here waits for one;
 *        a kind this side does not know is dropped without a count.
 */
static void
on_frame(void *ctx, uint8_t *buf, size_t len)
{
    tiku_link_console_t *lc = (tiku_link_console_t *)ctx;
    uint16_t wire_crc;
    uint8_t ctl, seq;

    if (len < TIKU_LINK_CONSOLE_OVERHEAD) {
        stats.bad++;
        return;
    }
    wire_crc = (uint16_t)(((uint16_t)buf[len - 2u] << 8) | buf[len - 1u]);
    if (tiku_link_console_crc(CRC_INIT, buf, len - 2u) != wire_crc) {
        stats.bad++;
        return;
    }
    ctl = buf[0];
    seq = buf[1];
    switch (ctl & TIKU_LINK_CONSOLE_KIND) {
    case TIKU_LINK_CONSOLE_SYN:
        lc->paced_expect = (uint8_t)(seq + 1u);
        lc->paced_synced = 1u;
        lc->tx_seq = 0u;            /* the peer counts this stream from zero */
        send_ack(lc, seq);
        break;
    case TIKU_LINK_CONSOLE_DATA:
        if (ctl & TIKU_LINK_CONSOLE_WANT_ACK) {
            paced(lc, seq, buf, len);
            break;
        }
        if (lc->plain_seen && seq != lc->plain_expect) {
            stats.gap++;
        }
        lc->plain_expect = (uint8_t)(seq + 1u);
        lc->plain_seen = 1u;
        tiku_link_deliver(&lc->link, buf + 2, len - TIKU_LINK_CONSOLE_OVERHEAD);
        stats.rx++;
        break;
    default:
        break;
    }
}

/*---------------------------------------------------------------------------*/
/* THE LINK                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Give the channel slot back and forget the receiver: a frame on
 *        the marker is text from here on.  The state stays the caller's,
 *        so the same link can be opened again.
 */
static void
console_close(tiku_link_t *l)
{
    tiku_link_console_t *lc = (tiku_link_console_t *)l->ctx;

    tiku_console_remove_channel(lc->marker, 0xFFu);
    l->recv = (tiku_link_recv_fn)0;
}

static const tiku_link_ops_t console_ops = {
    console_send, (void (*)(tiku_link_t *))0, console_close
};

tiku_link_t *
tiku_link_console_open(tiku_link_console_t *lc, uint8_t marker,
                       uint8_t *buf, size_t cap)
{
    if (lc == (tiku_link_console_t *)0) {
        return (tiku_link_t *)0;
    }
    lc->marker = marker;
    lc->tx_seq = 0u;
    lc->paced_expect = 0u;
    lc->paced_synced = 0u;
    lc->plain_expect = 0u;
    lc->plain_seen = 0u;
    lc->link.ops = &console_ops;
    lc->link.ctx = lc;
    lc->link.cap = TIKU_VFS_CAP_ALL;
    lc->link.recv = (tiku_link_recv_fn)0;
    lc->link.recv_ctx = (void *)0;
    if (tiku_console_add_channel(marker, 0xFFu, 0u, on_frame, lc,
                                 buf, cap) != 0) {
        return (tiku_link_t *)0;
    }
    return &lc->link;
}

/*---------------------------------------------------------------------------*/
/* OBSERVABILITY                                                             */
/*---------------------------------------------------------------------------*/

const tiku_link_console_stats_t *
tiku_link_console_stats(void)
{
    return &stats;
}

void
tiku_link_console_stats_reset(void)
{
    stats.rx = 0u;
    stats.tx = 0u;
    stats.bad = 0u;
    stats.dup = 0u;
    stats.reject = 0u;
    stats.acked = 0u;
    stats.gap = 0u;
}
