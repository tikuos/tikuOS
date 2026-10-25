/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_stream.c - sequenced CRC-32 chunks with two outstanding loans.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_stream.h"
#include <string.h>

#if TIKU_IPC_ENABLE
#define STREAM_MAGIC 0x31535049UL

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static uint32_t crc_bytes(uint32_t crc, const uint8_t *p, uint16_t length)
{
    unsigned bit;
    while (length--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320UL : 0);
    }
    return crc;
}

tiku_ipc_err_t tiku_ipc_stream_open(tiku_ipc_stream_t *s,
    tiku_ipc_endpoint_t endpoint, uint32_t object, uint32_t total)
{
    tiku_ipc_status_t status;
    if (s == NULL || !object || !total || !endpoint.slot_plus_one ||
        TIKU_THIS() == NULL) return TIKU_IPC_INVALID;
    if (s->pending[0].slot_plus_one || s->pending[1].slot_plus_one)
        return TIKU_IPC_BUSY;
    if (tiku_ipc_query(endpoint, &status) !=
            TIKU_IPC_OK || !status.open) return TIKU_IPC_INVALID;
    if (s->session == status.session && object <= s->object) return TIKU_IPC_INVALID;
    *s = (tiku_ipc_stream_t){0};
    s->endpoint = endpoint; s->object = object; s->total = total;
    s->session = status.session;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_stream_write(tiku_ipc_stream_t *s,
    const void *data, uint16_t length, tiku_clock_time_t timeout)
{
    unsigned i;
    uint16_t bytes;
    void *memory;
    uint8_t *p;
    tiku_ipc_buffer_t buffer;
    tiku_ipc_err_t result;
    if (s == NULL || data == NULL || !length || length > TIKU_IPC_STREAM_CHUNK ||
        s->failed || s->replacing || s->offset > s->total || length > s->total - s->offset)
        return TIKU_IPC_INVALID;
    for (i = 0; i < 2; i++) if (!s->pending[i].slot_plus_one) break;
    if (i == 2) return TIKU_IPC_AGAIN;
    result = tiku_ipc_buffer_alloc((uint16_t)(length + TIKU_IPC_STREAM_HEADER), &buffer);
    if (result != TIKU_IPC_OK) return result;
    result = tiku_ipc_buffer_map(buffer, &memory, &bytes);
    if (result != TIKU_IPC_OK) {
        (void)tiku_ipc_buffer_release(buffer);
        return result;
    }
    p = memory;
    put32(p, STREAM_MAGIC); put32(p + 4, s->session);
    put32(p + 8, s->object); put32(p + 12, s->total);
    put32(p + 16, s->offset);
    p[20] = (uint8_t)length; p[21] = (uint8_t)(length >> 8);
    p[22] = length == s->total - s->offset;
    p[23] = s->discontinuity && !s->offset;
    memcpy(p + TIKU_IPC_STREAM_HEADER, data, length);
    put32(p + 24, ~crc_bytes(crc_bytes(UINT32_MAX, p, 24),
                               p + TIKU_IPC_STREAM_HEADER, length));
    result = tiku_ipc_submit(s->endpoint, buffer, TIKU_IPC_INPUT,
                              timeout, &s->pending[i]);
    (void)tiku_ipc_buffer_release(buffer);
    if (result == TIKU_IPC_OK) s->offset += length;
    return result;
}

tiku_ipc_err_t tiku_ipc_stream_finish(tiku_ipc_stream_t *s,
    const tiku_ipc_completion_t *completion)
{
    unsigned i;
    if (s == NULL || completion == NULL) return TIKU_IPC_INVALID;
    for (i = 0; i < 2; i++) {
        if (s->pending[i].slot_plus_one == completion->submission.slot_plus_one &&
            s->pending[i].generation == completion->submission.generation) {
            s->pending[i] = (tiku_ipc_submission_t){0};
            if (completion->status != TIKU_IPC_OK || completion->quarantined)
                s->failed = 1;
            return completion->status;
        }
    }
    return TIKU_IPC_INVALID;
}

uint8_t tiku_ipc_stream_done(const tiku_ipc_stream_t *s)
{
    return s != NULL && s->total && s->offset == s->total && !s->failed &&
        !s->pending[0].slot_plus_one && !s->pending[1].slot_plus_one;
}

tiku_ipc_err_t tiku_ipc_stream_offer(tiku_ipc_stream_t *s,
    tiku_ipc_endpoint_t endpoint, uint32_t object, uint32_t total,
    tiku_ipc_stream_policy_t policy)
{
    tiku_ipc_err_t result;
    uint32_t dropped;
    uint8_t replacing = 0;
    tiku_ipc_status_t status;
    if (!s || !object || !total ||
        (policy != TIKU_IPC_STREAM_PRESERVE && policy != TIKU_IPC_STREAM_DROP_NEW &&
         policy != TIKU_IPC_STREAM_LATEST) ||
        tiku_ipc_query(endpoint,&status) != TIKU_IPC_OK)
        return TIKU_IPC_INVALID;
    if (s->total && !tiku_ipc_stream_done(s)) {
        if (object <= s->object) return TIKU_IPC_INVALID;
        if (policy == TIKU_IPC_STREAM_PRESERVE) return TIKU_IPC_AGAIN;
        if (policy == TIKU_IPC_STREAM_DROP_NEW) {
            if (s->last_dropped != object) { s->dropped++; s->last_dropped = object; }
            return TIKU_IPC_CANCELLED;
        }
        if (!s->replacing) { s->dropped++; s->replacing = 1; }
        for (unsigned i = 0; i < 2; i++)
            if (s->pending[i].slot_plus_one)
                (void)tiku_ipc_cancel(s->endpoint,s->pending[i]);
        if (s->pending[0].slot_plus_one || s->pending[1].slot_plus_one)
            return TIKU_IPC_AGAIN;
        replacing = 1;
    }
    dropped = s->dropped;
    result = tiku_ipc_stream_open(s,endpoint,object,total);
    if (result == TIKU_IPC_OK) {
        s->dropped = dropped;
        s->discontinuity = replacing;
    }
    return result;
}

tiku_ipc_err_t tiku_ipc_stream_read(tiku_ipc_stream_reader_t *r,
    tiku_ipc_endpoint_t endpoint, const tiku_ipc_received_t *message,
    const void **data, uint16_t *length)
{
    const void *memory;
    uint16_t bytes;
    tiku_ipc_err_t result;
    if (r == NULL || message == NULL || data == NULL || length == NULL ||
        !message->is_buffer) return TIKU_IPC_INVALID;
    result = tiku_ipc_loan_read(endpoint, message->submission, &memory, &bytes);
    if (result != TIKU_IPC_OK) return result;
    return tiku_ipc_stream_decode(r,memory,bytes,data,length);
}

tiku_ipc_err_t tiku_ipc_stream_decode(tiku_ipc_stream_reader_t *r,
    const void *memory,uint16_t bytes,const void **data,uint16_t *length)
{
    const uint8_t *p;
    uint32_t session, object, total, offset;
    uint16_t payload;
    if(!r||!memory||!data||!length)return TIKU_IPC_INVALID;
    if (bytes <= TIKU_IPC_STREAM_HEADER) return TIKU_IPC_IO;
    p = memory;
    payload = (uint16_t)((uint16_t)p[20] | (uint16_t)p[21] << 8);
    session = get32(p + 4); object = get32(p + 8);
    total = get32(p + 12); offset = get32(p + 16);
    if (get32(p) != STREAM_MAGIC || !session || !object || !total ||
        payload != bytes - TIKU_IPC_STREAM_HEADER || offset > total ||
        payload > total - offset || p[22] != (payload == total - offset) ||
        (p[23] & ~1u) || (p[23] && offset) ||
        get32(p + 24) != ~crc_bytes(crc_bytes(UINT32_MAX, p, 24),
            p + TIKU_IPC_STREAM_HEADER, payload)) return TIKU_IPC_IO;
    if (session != r->session || object != r->object) {
        if (offset || session < r->session ||
            (session == r->session && (object <= r->object ||
             (r->offset != r->total && !p[23]))))
            return TIKU_IPC_IO;
        if (session == r->session && r->offset != r->total) r->dropped++;
    } else if (total != r->total || offset != r->offset) return TIKU_IPC_IO;
    r->session = session; r->object = object; r->total = total;
    r->offset = offset + payload;
    *data = p + TIKU_IPC_STREAM_HEADER;
    *length = payload;
    return TIKU_IPC_OK;
}
#endif
