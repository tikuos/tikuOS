/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_radio_frames.c - whole-frame admission and streamed packet delivery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_radio_frames.h"
#include <string.h>
tiku_ipc_err_t tiku_radio_frame_push_if(tiku_radio_frames_t *q, uint8_t interface, const void *p, uint16_t n)
{
    if (!q || !p || interface > 1 || (interface && !q->tagged) || n < 14 || n > TIKU_RADIO_FRAME_MAX)
        return TIKU_IPC_INVALID;
    if (q->count == TIKU_RADIO_LOCAL_QUEUE) {
        q->drops++;
        return TIKU_IPC_AGAIN;
    }
    unsigned i = (q->head + q->count) % TIKU_RADIO_LOCAL_QUEUE;
    memcpy(q->data[i], p, n);
    q->length[i] = n;
    q->interface[i] = interface;
    q->complete[i] = 1;
    q->count++;
    return TIKU_IPC_OK;
}
tiku_ipc_err_t tiku_radio_frame_push(tiku_radio_frames_t *q, const void *p, uint16_t n)
{ return tiku_radio_frame_push_if(q, 0, p, n); }
const uint8_t *tiku_radio_frame_peek(tiku_radio_frames_t *q, uint16_t *n)
{
    if (!q || !n || !q->count || !q->complete[q->head])
        return NULL;
    *n = q->length[q->head];
    return q->data[q->head];
}
void tiku_radio_frame_pop(tiku_radio_frames_t *q)
{
    if (!q || !q->count || !q->complete[q->head])
        return;
    q->complete[q->head] = 0;
    q->length[q->head] = 0;
    q->head = (q->head + 1) % TIKU_RADIO_LOCAL_QUEUE;
    q->count--;
    q->delivered++;
}
tiku_ipc_err_t tiku_radio_frame_consume(void *context, const void *p, uint16_t n)
{
    tiku_radio_frames_t *q = context;
    tiku_ipc_stream_reader_t next = q->reader;
    const void *payload;
    uint16_t length;
    tiku_ipc_err_t r = tiku_ipc_stream_decode(&next, p, n, &payload, &length);
    if (r) {
        /* The two sender lanes can complete their wire fragments out of order. */
        if (n > TIKU_IPC_STREAM_HEADER) {
            const uint8_t *b = p;
            tiku_ipc_stream_reader_t future = {0};
            future.session = tiku_radio_get32(b + 4);
            future.object = tiku_radio_get32(b + 8);
            future.total = tiku_radio_get32(b + 12);
            future.offset = tiku_radio_get32(b + 16);
            uint8_t same = future.session == q->reader.session && future.object == q->reader.object;
            if (future.total >= 14 && future.total <= TIKU_RADIO_FRAME_MAX &&
                future.offset > (same ? q->reader.offset : 0) &&
                future.session >= q->reader.session && (!same || future.total == q->reader.total) &&
                (future.session > q->reader.session || future.object >= q->reader.object) &&
                tiku_ipc_stream_decode(&future, p, n, &payload, &length) == TIKU_IPC_OK)
                return TIKU_IPC_AGAIN;
        }
        q->bad++;
        return r;
    }
    if (next.total < 14 || next.total > TIKU_RADIO_FRAME_MAX) {
        q->bad++;
        return TIKU_IPC_INVALID;
    }
    if (!q->assembling) {
        if (next.offset != length) {
            q->bad++;
            return TIKU_IPC_INVALID;
        }
        if (q->count == TIKU_RADIO_LOCAL_QUEUE)
            return TIKU_IPC_AGAIN;
        q->assembly = (q->head + q->count) % TIKU_RADIO_LOCAL_QUEUE;
        q->complete[q->assembly] = 0;
        q->count++;
        q->assembling = 1;
    } else if (next.object != q->reader.object || next.session != q->reader.session) {
        q->drops++;
        q->length[q->assembly] = 0;
    }
    memcpy(q->data[q->assembly] + next.offset - length, payload, length);
    q->reader = next;
    q->deadline = tiku_clock_time() + 2 * TIKU_CLOCK_SECOND;
    if (next.offset == next.total) {
        q->length[q->assembly] = (uint16_t)next.total;
        q->interface[q->assembly] = q->tagged ? next.object & 1u : 0;
        q->complete[q->assembly] = 1;
        q->assembling = 0;
    }
    return TIKU_IPC_OK;
}
void tiku_radio_frame_expire(tiku_radio_frames_t *q, tiku_clock_time_t now)
{
    if (q->assembling && !TIKU_CLOCK_LT(now, q->deadline)) {
        q->count--;
        q->assembling = 0;
        q->reader.offset = q->reader.total;
        q->drops++;
    }
}
void tiku_radio_sender_cancel(tiku_radio_sender_t *s, tiku_ipc_endpoint_t ep)
{
    if (!s->active) {
        s->frames.count = 0;
        return;
    }
    if (s->frames.count > 1) {
        s->frames.drops += s->frames.count - 1;
        s->frames.count = 1;
    }
    s->stream.failed = 1;
    for (unsigned i = 0; i < 2; i++)
        if (s->stream.pending[i].slot_plus_one)
            (void)tiku_ipc_cancel(ep, s->stream.pending[i]);
}
void tiku_radio_sender_step(tiku_radio_sender_t *s, tiku_ipc_endpoint_t ep)
{
    tiku_ipc_completion_t completion;
    for (unsigned i = 0; i < 2 && tiku_ipc_completion(ep, &completion) == TIKU_IPC_OK; i++)
        (void)tiku_ipc_stream_finish(&s->stream, &completion);
    if (s->active && s->stream.failed)
        tiku_radio_sender_cancel(s, ep);
    if (s->active && (tiku_ipc_stream_done(&s->stream) ||
                      (s->stream.failed && !s->stream.pending[0].slot_plus_one &&
                       !s->stream.pending[1].slot_plus_one))) {
        uint8_t failed = s->stream.failed;
        if (failed)
            s->frames.drops++;
        tiku_radio_frame_pop(&s->frames);
        s->active = 0;
        if (failed)
            return;
    }
    uint16_t length;
    const uint8_t *p = tiku_radio_frame_peek(&s->frames, &length);
    if (!p)
        return;
    if (!s->active) {
        if (s->serial >= UINT32_MAX - 2)
            return;
        uint32_t object = s->serial + 1;
        if (s->frames.tagged) object = ((s->serial + 2) & ~1u) | s->frames.interface[s->frames.head];
        if (tiku_ipc_stream_open(&s->stream, ep, object, length) != TIKU_IPC_OK)
            return;
        s->serial = object;
        s->active = 1;
        s->stream.discontinuity = 1;
    }
    if (!s->stream.failed && s->stream.offset < length) {
        uint16_t n = (uint16_t)(length - s->stream.offset);
        if (n > TIKU_IPC_STREAM_CHUNK)
            n = TIKU_IPC_STREAM_CHUNK;
        (void)tiku_ipc_stream_write(&s->stream, p + s->stream.offset, n, 2 * TIKU_CLOCK_SECOND);
    }
}
