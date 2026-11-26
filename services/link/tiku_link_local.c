/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_local.c - nonblocking, ordered local link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link_local.h"
#include <string.h>

static int send_parts(tiku_link_t *link, const void *head, size_t hn,
                      const void *body, size_t bn)
{
    tiku_link_local_t *q = link->ctx;
    if (!q->storage || (!head && hn) || (!body && bn) || (!hn && !bn)) return -1;
    size_t limit = q->stride - sizeof(uint16_t);
    if (hn > limit || bn > limit - hn) {
        q->oversize++;
        return -1;
    }
    if (q->count == q->slots) {
        q->full++;
        return -1;
    }
    unsigned slot = (q->first + q->count) % q->slots;
    uint8_t *p = q->storage + (size_t)slot * q->stride;
    uint16_t len = (uint16_t)(hn + bn);
    memcpy(p, &len, sizeof len);
    if (hn) memcpy(p + sizeof len, head, hn);
    if (bn) memcpy(p + sizeof len + hn, body, bn);
    q->count++;
    if (q->wake) q->wake();
    return 0;
}

static void close_queue(tiku_link_t *link)
{
    tiku_link_local_t *q = link->ctx;
    q->storage = NULL;
    q->first = q->count = 0;
    tiku_link_on_recv(link, NULL, NULL);
    if (q->wake) q->wake();
}

static const tiku_link_ops_t ops = {send_parts, NULL, close_queue};

int tiku_link_local_open(tiku_link_local_t *q, uint8_t *storage,
                         unsigned slots, unsigned stride)
{
    if (!q || !storage || !slots || stride <= sizeof(uint16_t) ||
        stride > UINT16_MAX || slots > SIZE_MAX / stride) return -1;
    *q = (tiku_link_local_t){.storage = storage, .slots = slots, .stride = stride};
    q->link.ops = &ops;
    q->link.ctx = q;
    return 0;
}

const uint8_t *tiku_link_local_front(const tiku_link_local_t *q, uint16_t *length)
{
    if (!q || !q->storage || !q->count || !length) return NULL;
    const uint8_t *p = q->storage + (size_t)q->first * q->stride;
    memcpy(length, p, sizeof *length);
    return p + sizeof *length;
}

int tiku_link_local_read(tiku_link_local_t *q, void *out, size_t capacity)
{
    uint16_t len;
    if (!q || !q->storage || !out) return -1;
    if (!q->count) return 0;
    const uint8_t *p = tiku_link_local_front(q, &len);
    if (!p || len > capacity) return -1;
    memcpy(out, p, len);
    q->first = (q->first + 1) % q->slots;
    q->count--;
    return len;
}
