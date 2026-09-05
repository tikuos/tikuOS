/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.c - the link contract's calls, each a dispatch to the backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_link.h"

int
tiku_link_send(tiku_link_t *l, const void *msg, size_t len)
{
    return tiku_link_send2(l, msg, len, (const void *)0, 0u);
}

int
tiku_link_send2(tiku_link_t *l, const void *head, size_t hlen,
                const void *body, size_t blen)
{
    if (l == (tiku_link_t *)0 || l->ops == (const tiku_link_ops_t *)0 ||
        l->ops->send == (int (*)(tiku_link_t *, const void *, size_t,
                                 const void *, size_t))0) {
        return -1;
    }
    return l->ops->send(l, head, hlen, body, blen);
}

void
tiku_link_on_recv(tiku_link_t *l, tiku_link_recv_fn fn, void *ctx)
{
    if (l != (tiku_link_t *)0) {
        l->recv = fn;
        l->recv_ctx = ctx;
    }
}

void
tiku_link_deliver(tiku_link_t *l, uint8_t *msg, size_t len)
{
    if (l != (tiku_link_t *)0 && l->recv != (tiku_link_recv_fn)0) {
        l->recv(l->recv_ctx, msg, len);
    }
}

void
tiku_link_pump(tiku_link_t *l)
{
    if (l != (tiku_link_t *)0 && l->ops != (const tiku_link_ops_t *)0 &&
        l->ops->pump != (void (*)(tiku_link_t *))0) {
        l->ops->pump(l);
    }
}

uint8_t
tiku_link_cap(const tiku_link_t *l)
{
    return (l != (const tiku_link_t *)0) ? l->cap : 0u;
}

void
tiku_link_close(tiku_link_t *l)
{
    if (l != (tiku_link_t *)0 && l->ops != (const tiku_link_ops_t *)0 &&
        l->ops->close != (void (*)(tiku_link_t *))0) {
        l->ops->close(l);
    }
}
