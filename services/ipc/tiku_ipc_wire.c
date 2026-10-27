/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_wire.c - fragmentation, credits, duplicate checks and epochs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_wire.h"
#include <string.h>
#if TIKU_IPC_ENABLE
enum { HELLO = 1, HELLO_ACK, COPY, COPY_ACK, INPUT, INPUT_ACK, OUTPUT, OUTPUT_DATA };
#define RETRY_TICKS 4u
#define COPY_TICKS 128u

/** @brief Decode a little-endian word of at most eight bytes. */
static uint64_t get(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint64_t)p[i] << (8u*i);
    return value;
}
/** @brief Encode a little-endian word of at most eight bytes. */
static void put(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (uint8_t)(value >> (8u*i));
}
/** @brief Continue the reflected CRC-32/ISO-HDLC accumulator. */
static uint32_t crc(uint32_t value, const uint8_t *data, uint16_t length)
{
    for (unsigned i = 0; i < length; i++) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ ((value & 1u) ? 0xedb88320UL : 0);
    }
    return value;
}
/** @brief Copy one frame to the carrier; refusal retains the source state. */
static int emit(tiku_ipc_wire_t *c, uint8_t kind, uint32_t id, uint16_t offset,
                uint16_t total, tiku_ipc_err_t result, const void *data, uint16_t length)
{
    uint8_t frame[TIKU_IPC_WIRE_FRAME] = {0};
    uint16_t limit = c->ready && c->peer_frame < c->frame_size
                     ? c->peer_frame : c->frame_size;
    if (length > limit - TIKU_IPC_WIRE_HEADER) return -1;
    put(frame, 0x50494b54UL, 4);
    frame[4] = 1; frame[5] = kind;
    put(frame+8, c->epoch, 8); put(frame+16, kind == HELLO ? 0 : c->peer_epoch, 8);
    put(frame+24, id, 4); put(frame+28, offset, 2); put(frame+30, total, 2);
    put(frame+32, length, 2); put(frame+34, (uint16_t)(int16_t)result, 2);
    if (length) memcpy(frame+TIKU_IPC_WIRE_HEADER, data, length);
    put(frame+36, ~crc(crc(UINT32_MAX, frame, 36), frame+40, length), 4);
    return tiku_link_send(c->link, frame, (size_t)length + TIKU_IPC_WIRE_HEADER);
}
/** @brief Reserve control and each bulk lane's separate receive frame. */
static void receive(void *context, uint8_t *frame, size_t length)
{
    tiku_ipc_wire_t *c = context;
    uint32_t id;
    unsigned lane;
    if (length < TIKU_IPC_WIRE_HEADER || length > c->frame_size ||
        get(frame,4) != 0x50494b54UL || frame[4] != 1 || frame[6] || frame[7] ||
        get(frame+32,2) != length - TIKU_IPC_WIRE_HEADER ||
        get(frame+36,4) != ~crc(crc(UINT32_MAX, frame, 36), frame+40,
                              (uint16_t)(length-40))) { c->bad++; return; }
    id = (uint32_t)get(frame+24,4);
    lane = frame[5] <= COPY_ACK ? 0u : id & 3u;
    if (lane > 2 || (frame[5] > COPY_ACK && !lane)) { c->bad++; return; }
    if (c->inbox[lane].length) {
        if (!lane || !c->ready || get(frame+8,8) != c->peer_epoch ||
            get(frame+16,8) != c->epoch ||
            id <= get(c->inbox[lane].data+24,4)) return;
    }
    memcpy(c->inbox[lane].data, frame, length);
    c->inbox[lane].length = (uint16_t)length;
    (void)tiku_ipc_service_at(c->endpoint, 0);
}
/** @brief Invalidate local work while fencing all later private copy-out. */
static void lose(tiku_ipc_wire_t *c)
{
    for (unsigned i = 0; i < 2; i++) {
        if (c->client[i].busy)
            (void)tiku_ipc_complete(c->client[i].transfer.submission,
                c->client[i].transfer.session, TIKU_IPC_DOWN);
        c->client[i].busy = 0;
        memset(&c->server[i], 0, sizeof c->server[i]);
    }
    c->control.busy = c->ready = 0;
    c->control_seen = 0;
    (void)tiku_ipc_peer_reset(c->endpoint);
    {
        tiku_ipc_status_t status;
        if (tiku_ipc_status(c->endpoint,&status) == TIKU_IPC_OK)
            c->local_session = status.session;
    }
}
/** @brief Negotiate the endpoint name, logical caps and physical frame limit. */
static int hello(tiku_ipc_wire_t *c, uint8_t kind)
{
    uint8_t data[40] = {0};
    tiku_ipc_status_t local;
    uint16_t copied = c->frame_size-40 < 64 ? c->frame_size-40 : 64;
    size_t n = strlen(c->name) + 1;
    if (tiku_ipc_status(c->endpoint,&local) != TIKU_IPC_OK || !local.max_message) return -1;
    if (copied > local.max_message) copied = local.max_message;
    memcpy(data, c->name, n);
    put(data+n, c->frame_size,2);
    put(data+n+2, copied,2);
    put(data+n+4, TIKU_IPC_WIRE_BLOCK,2);
    data[n+6] = c->sink ? (uint8_t)((c->sink->consume ? 1u : 0u) |
                                  (c->sink->produce ? 2u : 0u)) : 0;
    return emit(c, kind, 0, 0, 0, TIKU_IPC_OK, data, (uint16_t)(n+7));
}
/** @brief Consume a valid hello without admitting mismatched names or epochs. */
static void negotiate(tiku_ipc_wire_t *c, const uint8_t *p)
{
    const uint8_t *data = p+40;
    uint16_t length = (uint16_t)get(p+32,2), frame;
    uint64_t peer = get(p+8,8), dest = get(p+16,8);
    size_t n = strlen(c->name)+1;
    if (!peer || length != n+7 || memcmp(data,c->name,n) ||
        (p[5] == HELLO_ACK && dest != c->epoch) || (dest && dest != c->epoch)) {
        c->stale++; return;
    }
    frame = (uint16_t)get(data+n,2);
    if (frame < 80 || frame > TIKU_IPC_WIRE_FRAME || !get(data+n+2,2) ||
        get(data+n+2,2) > 64 || get(data+n+2,2) > frame-TIKU_IPC_WIRE_HEADER ||
        get(data+n+4,2) > TIKU_IPC_WIRE_BLOCK ||
        data[n+6] > 3) { c->bad++; return; }
    uint8_t was_ready = c->ready;
    uint8_t changed = c->peer_epoch && peer != c->peer_epoch;
    if (changed) {
        if (p[5] == HELLO && was_ready && c->epoch == UINT64_MAX) {
            c->retired = 1; return;
        }
        lose(c);
        if (p[5] == HELLO && was_ready) c->epoch++;
    }
    c->peer_epoch = peer;
    c->peer_frame = frame;
    c->peer_message = (uint16_t)get(data+n+2,2);
    if (c->peer_message > c->frame_size-TIKU_IPC_WIRE_HEADER)
        c->peer_message = (uint16_t)(c->frame_size-TIKU_IPC_WIRE_HEADER);
    c->peer_buffer = (uint16_t)get(data+n+4,2);
    c->peer_directions = data[n+6];
    if (p[5] == HELLO) {
        c->ready = was_ready && !changed;
        c->hello_reply = 1;
        if (!c->ready) c->hello_at = tiku_clock_time();
    } else {
        c->ready = 1;
        if (!was_ready || changed) c->hello_reply = 1;
    }
}
/** @brief Answer copied admission, with one remembered result for duplicates. */
static int copied(tiku_ipc_wire_t *c, const uint8_t *p)
{
    uint32_t id = (uint32_t)get(p+24,4);
    uint16_t length = (uint16_t)get(p+32,2);
    tiku_ipc_status_t status;
    if ((id & 3u) || !id || length > 64) { c->bad++; return 1; }
    if (id < c->control_seen) { c->stale++; return 1; }
    if (id == c->control_seen) c->duplicate++;
    else {
        tiku_ipc_err_t result = tiku_ipc_status(c->endpoint, &status);
        if (result == TIKU_IPC_OK)
            result = tiku_ipc_deliver(c->endpoint, status.session, p+40, length);
        if (result == TIKU_IPC_AGAIN) return 0;
        c->control_result = result;
        c->control_seen = id;
        if (result == TIKU_IPC_OK) c->delivered++;
    }
    return emit(c,COPY_ACK,id,0,0,c->control_result,NULL,0) == 0;
}
/** @brief Assemble input or produce output in one peer-owned staging lane. */
static int request(tiku_ipc_wire_t *c, const uint8_t *p)
{
    uint32_t id = (uint32_t)get(p+24,4);
    uint16_t offset = (uint16_t)get(p+28,2), total = (uint16_t)get(p+30,2);
    uint16_t length = (uint16_t)get(p+32,2), amount;
    unsigned lane = (id & 3u)-1;
    if (!total || total > TIKU_IPC_WIRE_BLOCK || offset > total ||
        length > total-offset || (p[5] == OUTPUT && length)) { c->bad++; return 1; }
    if (id < c->server[lane].id) { c->stale++; return 1; }
    if (id != c->server[lane].id) {
        if (offset) { c->bad++; return 1; }
        memset(&c->server[lane],0,sizeof c->server[lane]);
        c->server[lane].id = id;
        c->server[lane].total = total;
        c->server[lane].kind = p[5];
    }
    if (total != c->server[lane].total || p[5] != c->server[lane].kind) {
        c->bad++; return 1;
    }
    if (!c->server[lane].done && p[5] == INPUT) {
        if (!length || offset > c->server[lane].offset) { c->bad++; return 1; }
        if (offset < c->server[lane].offset) {
            if (length > c->server[lane].offset-offset ||
                memcmp(c->server[lane].data+offset,p+40,length)) { c->bad++; return 1; }
            c->duplicate++;
        } else {
            memcpy(c->server[lane].data+offset,p+40,length);
            c->server[lane].offset = (uint16_t)(offset+length);
        }
    }
    if (!c->server[lane].done &&
        (p[5] == OUTPUT || c->server[lane].offset == total)) {
        tiku_ipc_err_t result = TIKU_IPC_UNSUPPORTED;
        if (c->sink && p[5] == INPUT && c->sink->consume)
            result = c->sink->consume(c->sink->context,c->server[lane].data,total);
        if (c->sink && p[5] == OUTPUT && c->sink->produce)
            result = c->sink->produce(c->sink->context,c->server[lane].data,total);
        if (result == TIKU_IPC_AGAIN) return 0;
        c->server[lane].result = result;
        c->server[lane].done = 1;
    }
    if (p[5] == INPUT)
        return emit(c,INPUT_ACK,id,c->server[lane].offset,total,
                    c->server[lane].result,NULL,0) == 0;
    amount = (uint16_t)(total-offset);
    if (amount > c->peer_frame-40) amount = (uint16_t)(c->peer_frame-40);
    if (amount > c->frame_size-40) amount = (uint16_t)(c->frame_size-40);
    if (c->server[lane].result != TIKU_IPC_OK) amount = 0;
    return emit(c,OUTPUT_DATA,id,offset,total,c->server[lane].result,
                c->server[lane].data+offset,amount) == 0;
}
/** @brief Apply a matched fragment response; stale replies retain no access. */
static void response(tiku_ipc_wire_t *c, const uint8_t *p)
{
    uint32_t id = (uint32_t)get(p+24,4);
    uint16_t offset = (uint16_t)get(p+28,2), total = (uint16_t)get(p+30,2);
    uint16_t length = (uint16_t)get(p+32,2);
    tiku_ipc_err_t result = (tiku_ipc_err_t)(int16_t)get(p+34,2);
    unsigned lane = (id & 3u)-1;
    if (p[5] == COPY_ACK) {
        if (c->control.busy && c->control.id == id) {
            c->control.busy = 0;
            if (result != TIKU_IPC_OK) c->failed++;
        } else c->stale++;
        return;
    }
    if (!c->client[lane].busy || c->client[lane].id != id) { c->stale++; return; }
    if (total != c->client[lane].transfer.length || offset > total) { c->bad++; return; }
    if (result == TIKU_IPC_OK && p[5] == INPUT_ACK &&
        c->client[lane].transfer.direction == TIKU_IPC_INPUT) {
        uint16_t step = (uint16_t)(c->peer_frame-40);
        if (step > c->frame_size-40) step = (uint16_t)(c->frame_size-40);
        if (step > total-c->client[lane].offset) step = (uint16_t)(total-c->client[lane].offset);
        if (length || offset != c->client[lane].offset+step) { c->stale++; return; }
        c->client[lane].offset = offset;
    } else if (result == TIKU_IPC_OK && p[5] == OUTPUT_DATA &&
               c->client[lane].transfer.direction == TIKU_IPC_OUTPUT) {
        if (offset != c->client[lane].offset || !length || length > total-offset) {
            c->stale++; return;
        }
        memcpy((uint8_t *)c->client[lane].transfer.data+offset,p+40,length);
        c->client[lane].offset = (uint16_t)(offset+length);
    } else if (result == TIKU_IPC_OK) { c->bad++; return; }
    if (result != TIKU_IPC_OK || c->client[lane].offset == total) {
        (void)tiku_ipc_complete(c->client[lane].transfer.submission,
            c->client[lane].transfer.session,result);
        c->client[lane].busy = 0;
    } else c->client[lane].retry = tiku_clock_time();
}

/** @brief Handle one reserved inbox, retaining it when admission is blocked. */
static void inbox_step(tiku_ipc_wire_t *c,unsigned i)
{
    uint8_t *p=c->inbox[i].data;
    if (p[5] == HELLO || p[5] == HELLO_ACK) negotiate(c,p);
    else if (!c->ready || get(p+8,8) != c->peer_epoch || get(p+16,8) != c->epoch) c->stale++;
    else if (p[5] == COPY) { if (!copied(c,p)) return; }
    else if (p[5] == INPUT || p[5] == OUTPUT) { if (!request(c,p)) return; }
    else if (p[5] == COPY_ACK || p[5] == INPUT_ACK || p[5] == OUTPUT_DATA) response(c,p);
    else c->bad++;
    c->inbox[i].length=0;
}

void tiku_ipc_wire_pump(tiku_ipc_wire_t *c, uint16_t budget)
{
    tiku_clock_time_t now = tiku_clock_time();
    if (!c || !c->initialized || c->retired || !budget) return;
    {
        tiku_ipc_status_t status;
        if (tiku_ipc_status(c->endpoint,&status) != TIKU_IPC_OK) return;
        if (c->local_session && status.session != c->local_session) {
            if (c->epoch == UINT64_MAX) { c->retired = 1; return; }
            lose(c);
            c->epoch++;
            c->peer_epoch = 0;
            memset(c->inbox,0,sizeof c->inbox);
            c->hello_at = now;
        } else c->local_session = status.session;
    }
    tiku_link_pump(c->link);
    if (!c->ready && !TIKU_CLOCK_LT(now,c->hello_at)) {
        (void)hello(c,HELLO);
        c->hello_at = (tiku_clock_time_t)(now+RETRY_TICKS);
    }
    if(budget&&c->inbox[0].length){budget--;inbox_step(c,0);}
    if (c->hello_reply && hello(c,HELLO_ACK) == 0) c->hello_reply = 0;
    if (c->ready && c->control.busy && !TIKU_CLOCK_LT(now,c->control.retry)) {
        if (!TIKU_CLOCK_LT(now,c->control.deadline)) { c->control.busy = 0; c->failed++; }
        else {
            int sent = emit(c,COPY,c->control.id,0,0,TIKU_IPC_OK,c->control.data,c->control.length);
            c->control.retry = (tiku_clock_time_t)(now+(sent ? 1u : RETRY_TICKS));
        }
    }
    for(unsigned i=1;i<3&&budget;i++)
        if(c->inbox[i].length){budget--;inbox_step(c,i);}
    /* Rotate accepted bulk frames across a carrier's shared queue slot. */
    unsigned first = c->client_cursor;
    for (unsigned n = 0; c->ready && n < 2; n++) {
        unsigned i = (first + n) % 2u;
        uint16_t amount, offset;
        if (!c->client[i].busy || TIKU_CLOCK_LT(now,c->client[i].retry)) continue;
        offset = c->client[i].offset;
        amount = (uint16_t)(c->client[i].transfer.length-offset);
        if (amount > c->peer_frame-40) amount = (uint16_t)(c->peer_frame-40);
        if (amount > c->frame_size-40) amount = (uint16_t)(c->frame_size-40);
        int sent = emit(c,c->client[i].transfer.direction == TIKU_IPC_INPUT ? INPUT : OUTPUT,
            c->client[i].id,offset,c->client[i].transfer.length,TIKU_IPC_OK,
            (uint8_t *)c->client[i].transfer.data+offset,
            c->client[i].transfer.direction == TIKU_IPC_INPUT ? amount : 0);
        c->client[i].retry = (tiku_clock_time_t)(now+(sent ? 1u : RETRY_TICKS));
        if (!sent) c->client_cursor = (uint8_t)((i + 1u) % 2u);
    }
    (void)tiku_ipc_service_at(c->endpoint,1);
}

/** @brief Start a loan only after peer limits and direction are negotiated. */
static tiku_ipc_err_t start(void *context, uint16_t endpoint,
                             const tiku_ipc_transfer_t *transfer)
{
    tiku_ipc_wire_t *c = context;
    unsigned i;
    if (!c || !c->initialized || endpoint != c->endpoint || c->retired) return TIKU_IPC_DOWN;
    if (!c->ready) return TIKU_IPC_AGAIN;
    if (transfer->length > c->peer_buffer ||
        !(c->peer_directions & (transfer->direction == TIKU_IPC_INPUT ? 1u : 2u)))
        return TIKU_IPC_UNSUPPORTED;
    for (i = 0; i < 2 && c->client[i].busy; i++) {}
    if (i == 2) return TIKU_IPC_AGAIN;
    if (c->serial == 0x3fffffffUL) return TIKU_IPC_DOWN;
    c->client[i].transfer = *transfer;
    c->client[i].id = (++c->serial << 2) | (i+1u);
    c->client[i].offset = 0;
    c->client[i].retry = tiku_clock_time();
    c->client[i].busy = 1;
    (void)tiku_ipc_service_at(endpoint,0);
    return TIKU_IPC_OK;
}
/** @brief Copy one control message into capacity reserved outside bulk lanes. */
static tiku_ipc_err_t send(void *context, uint16_t endpoint, uint32_t session,
                            const void *data, uint16_t length)
{
    tiku_ipc_wire_t *c = context;
    if (!c || !c->initialized || endpoint != c->endpoint || c->retired) return TIKU_IPC_DOWN;
    if (!c->ready || c->control.busy) return TIKU_IPC_AGAIN;
    if (length > c->peer_message || c->serial == 0x3fffffffUL) return TIKU_IPC_INVALID;
    memcpy(c->control.data,data,length);
    c->control.length = length;
    c->control.id = ++c->serial << 2;
    c->control.session = session;
    c->control.retry = tiku_clock_time();
    c->control.deadline = (tiku_clock_time_t)(tiku_clock_time()+COPY_TICKS);
    c->control.busy = 1;
    (void)tiku_ipc_service_at(endpoint,0);
    return TIKU_IPC_OK;
}
/** @brief Remove the matching private mapping before any later frame is applied. */
static uint8_t quiesce(void *context, uint16_t endpoint, const tiku_ipc_transfer_t *t)
{
    tiku_ipc_wire_t *c = context;
    (void)endpoint;
    for (unsigned i = 0; i < 2; i++)
        if (c->client[i].busy && c->client[i].transfer.submission.generation == t->submission.generation &&
            c->client[i].transfer.submission.slot_plus_one == t->submission.slot_plus_one)
            c->client[i].busy = 0;
    return 1;
}
/** @brief Advance the logical connection epoch without reusing old identities. */
static tiku_ipc_err_t reset(void *context, uint16_t endpoint)
{
    tiku_ipc_wire_t *c = context;
    if (!c || !c->initialized) return TIKU_IPC_INVALID;
    if (c->epoch == UINT64_MAX) { c->retired = 1; return TIKU_IPC_DOWN; }
    lose(c);
    c->epoch++;
    c->peer_epoch = 0;
    c->local_session = 0;
    c->hello_at = tiku_clock_time();
    memset(c->inbox,0,sizeof c->inbox);
    (void)tiku_ipc_service_at(endpoint,0);
    return TIKU_IPC_OK;
}
/** @brief Adapt one bounded core progress pass to link service. */
static void progress(void *context, uint16_t endpoint, uint16_t budget)
{ (void)endpoint; tiku_ipc_wire_pump(context,budget); }
/** @brief Expose negotiated limits without pumping the carrier. */
static void status(void *context, tiku_ipc_status_t *out)
{
    const tiku_ipc_wire_t *c = context;
    out->wire_bad = c->bad;
    out->wire_stale = c->stale;
    out->wire_duplicates = c->duplicate;
    out->wire_failed = c->failed;
    if (c->frame_size >= TIKU_IPC_WIRE_HEADER &&
        out->max_message > c->frame_size-TIKU_IPC_WIRE_HEADER)
        out->max_message = (uint16_t)(c->frame_size-TIKU_IPC_WIRE_HEADER);
    if (c->ready) {
        if (out->max_message > c->peer_message) out->max_message = c->peer_message;
        if (out->max_buffer > c->peer_buffer) out->max_buffer = c->peer_buffer;
        if (!c->peer_directions) out->methods &= ~(TIKU_IPC_CAP_LOAN | TIKU_IPC_CAP_STREAM);
    }
}
const tiku_ipc_backend_ops_t tiku_ipc_wire_ops = {
    .send = send, .start = start, .progress = progress, .quiesce = quiesce,
    .max_message = 64, .max_buffer = TIKU_IPC_WIRE_BLOCK,
    .status = status, .reset = reset, .reset_scope = TIKU_IPC_RESET_CONNECTION
};

tiku_ipc_err_t tiku_ipc_wire_init(tiku_ipc_wire_t *c, tiku_link_t *link,
    const char *name, uint16_t endpoint, uint64_t epoch, uint16_t frame_size,
    const tiku_ipc_wire_sink_t *sink)
{
    if (!c || c->initialized || !link || !link->ops || !link->ops->send ||
        !name || !name[0] || strlen(name) > 30 || !epoch ||
        frame_size < 80 || frame_size > TIKU_IPC_WIRE_FRAME ||
        endpoint >= tiku_ipc_endpoint_count()) return TIKU_IPC_INVALID;
    c->link = link; c->name = name; c->endpoint = endpoint;
    c->epoch = epoch; c->frame_size = frame_size; c->sink = sink;
    c->initialized = 1;
    tiku_link_on_recv(link,receive,c);
    (void)tiku_ipc_service_at(endpoint,0);
    return TIKU_IPC_OK;
}
#endif
