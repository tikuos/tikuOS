/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_console.c - the console backend of the link: one frame per
 * message on a marked channel, delivered from the console's pump.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_link_console.h"
#include <kernel/console/tiku_console.h>
#include <kernel/vfs/tiku_vfs.h>          /* TIKU_VFS_CAP_ALL */

/** @brief The console's channel handler: the frame is the message. */
static void
on_frame(void *ctx, uint8_t *buf, size_t len)
{
    tiku_link_deliver((tiku_link_t *)ctx, buf, len);
}

static int
console_send(tiku_link_t *l, const void *head, size_t hlen,
             const void *body, size_t blen)
{
    const tiku_link_console_t *lc = (const tiku_link_console_t *)l->ctx;

    return tiku_console_send_frame(lc->marker, head, hlen, body, blen);
}

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
    lc->link.ops = &console_ops;
    lc->link.ctx = lc;
    lc->link.cap = TIKU_VFS_CAP_ALL;
    lc->link.recv = (tiku_link_recv_fn)0;
    lc->link.recv_ctx = (void *)0;
    if (tiku_console_add_channel(marker, 0xFFu, 0u, on_frame, &lc->link,
                                 buf, cap) != 0) {
        return (tiku_link_t *)0;
    }
    return &lc->link;
}
