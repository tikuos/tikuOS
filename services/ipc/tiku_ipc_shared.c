/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_shared.c - CRC-checked staging and completion through fixed slots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_shared.h"

#if TIKU_IPC_ENABLE
tiku_ipc_err_t tiku_ipc_shared_init(tiku_ipc_shared_context_t *c,
    const tiku_ipc_shared_mapping_t *m, uint8_t owner)
{
    uintptr_t base, ring;
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (TIKU_THIS() != NULL) return TIKU_IPC_INVALID;
    if (c == NULL || m == NULL || c->claimed || !owner || !m->ring ||
        !m->claim_base || !m->claim_size || !m->barrier || !m->clean ||
        !m->invalidate || !m->ready || !m->poll_ticks ||
        (m->region_type != TIKU_MEM_REGION_SHARED &&
         m->region_type != TIKU_MEM_REGION_SRAM) ||
        m->poll_ticks > TIKU_CLOCK_MAX_INTERVAL) return TIKU_IPC_INVALID;
    base = (uintptr_t)m->claim_base; ring = (uintptr_t)m->ring;
    if (ring % TIKU_IPC_SHARED_LINE || ring < base ||
        ring - base > m->claim_size ||
        sizeof(*m->ring) > m->claim_size - (ring - base) ||
        !tiku_region_contains(m->claim_base, m->claim_size, m->region_type))
        return TIKU_IPC_INVALID;
    if (tiku_region_claim(m->claim_base, m->claim_size, owner) != TIKU_MEM_OK)
        return TIKU_IPC_BUSY;
    c->mapping = m;
    c->claimed = 1;
    return TIKU_IPC_OK;
}

static tiku_ipc_err_t session(tiku_ipc_shared_context_t *c, uint16_t endpoint)
{
    const tiku_ipc_shared_mapping_t *m = c->mapping;
    tiku_ipc_shared_t *r = m->ring;
    unsigned i;
    uint8_t invalidated = 0;
    if (c->retired || !m->ready()) return TIKU_IPC_DOWN;
    m->invalidate(&r->host, sizeof r->host);
    m->invalidate(&r->peer, sizeof r->peer);
    m->barrier();
    if (!c->epoch || r->host.magic != TIKU_IPC_SHARED_MAGIC ||
        r->host.epoch != c->epoch ||
        (r->peer.magic == TIKU_IPC_SHARED_RESTART && r->peer.epoch == c->epoch) ||
        (!c->negotiating && (r->peer.magic != TIKU_IPC_SHARED_MAGIC ||
                             r->peer.epoch != c->epoch))) {
        uint32_t previous = c->epoch;
        if (previous) {
            (void)tiku_ipc_peer_reset(endpoint);
            invalidated = 1;
            for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) c->orphan[i] = 1;
            c->control_pending = 0;
        }
        if (r->host.magic == TIKU_IPC_SHARED_MAGIC && r->host.epoch > previous)
            previous = r->host.epoch;
        if (previous == UINT32_MAX) { c->retired = 1; return TIKU_IPC_DOWN; }
        c->epoch = previous + 1u;
        c->negotiating = 1;
        r->host.epoch = c->epoch;
        r->host.magic = TIKU_IPC_SHARED_MAGIC;
        m->clean(&r->host, sizeof r->host);
        m->barrier();
    }
    m->invalidate(&r->peer, sizeof r->peer);
    m->barrier();
    if (r->peer.magic != TIKU_IPC_SHARED_MAGIC || r->peer.epoch != c->epoch) {
        (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
        return TIKU_IPC_AGAIN;
    }
    if (c->negotiating) {
        for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) c->busy[i] = 0;
        c->negotiating = 0;
    }
    if (invalidated) {
        (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
        return TIKU_IPC_AGAIN;
    }
    return TIKU_IPC_OK;
}

static tiku_ipc_err_t start(void *context, uint16_t endpoint,
                            const tiku_ipc_transfer_t *transfer)
{
    tiku_ipc_shared_context_t *c = context;
    const tiku_ipc_shared_mapping_t *m;
    tiku_ipc_shared_slot_t *slot;
    unsigned i, n;
    tiku_ipc_err_t result;
    if (c == NULL || !c->claimed || transfer->length > TIKU_IPC_SHARED_PAYLOAD)
        return TIKU_IPC_INVALID;
    m = c->mapping;
    result = session(c, endpoint);
    if (result != TIKU_IPC_OK) return result;
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++)
        if (!c->busy[i] && c->sequence[i] != UINT32_MAX) break;
    if (i == TIKU_IPC_SHARED_SLOTS) return TIKU_IPC_AGAIN;
    slot = &m->ring->slots[i];
    /* Withdraw the retained sequence before its descriptor can match an epoch. */
    slot->request.sequence = 0;
    m->clean(&slot->request, sizeof slot->request);
    m->barrier();
    c->active[i] = *transfer;
    c->busy[i] = 1; c->orphan[i] = 0;
    if (transfer->direction == TIKU_IPC_INPUT)
        for (n = 0; n < transfer->length; n++)
            slot->payload[n] = ((const uint8_t *)transfer->data)[n];
    slot->request.epoch = c->epoch;
    slot->request.length = transfer->length;
    slot->request.kind = transfer->direction == TIKU_IPC_INPUT
                         ? TIKU_IPC_SHARED_INPUT : TIKU_IPC_SHARED_OUTPUT;
    slot->request.crc = ~tiku_ipc_shared_crc(UINT32_MAX, slot->payload,
                                            transfer->length);
    m->clean((const void *)slot->payload, sizeof slot->payload);
    m->barrier();
    slot->request.sequence = ++c->sequence[i];
    m->clean(&slot->request, sizeof slot->request);
    m->barrier();
    (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
    return TIKU_IPC_OK;
}

static tiku_ipc_err_t send(void *context, uint16_t endpoint, uint32_t epoch,
                            const void *data, uint16_t length)
{
    tiku_ipc_shared_context_t *c = context;
    const tiku_ipc_shared_mapping_t *m;
    unsigned n;
    tiku_ipc_err_t result;
    if (c == NULL || !c->claimed || length > 64) return TIKU_IPC_INVALID;
    if (c->control_pending) return TIKU_IPC_AGAIN;
    if (c->control_sequence == UINT32_MAX) return TIKU_IPC_DOWN;
    m = c->mapping;
    result = session(c, endpoint);
    if (result != TIKU_IPC_OK) return result;
    m->ring->host.control_sequence = 0;
    m->clean(&m->ring->host, sizeof m->ring->host);
    m->barrier();
    for (n = 0; n < length; n++)
        m->ring->host.control_data[n] = ((const uint8_t *)data)[n];
    m->ring->host.control_epoch = c->epoch;
    m->ring->host.control_length = length;
    m->ring->host.control_crc = ~tiku_ipc_shared_crc(UINT32_MAX,
        m->ring->host.control_data, length);
    m->clean(&m->ring->host, sizeof m->ring->host);
    m->barrier();
    m->ring->host.control_sequence = ++c->control_sequence;
    m->clean(&m->ring->host, sizeof m->ring->host);
    m->barrier();
    c->control_session = epoch;
    c->control_endpoint = endpoint;
    c->control_pending = 1;
    (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
    return TIKU_IPC_OK;
}

static void progress(void *context, uint16_t endpoint, uint16_t budget)
{
    tiku_ipc_shared_context_t *c = context;
    const tiku_ipc_shared_mapping_t *m;
    unsigned i, n;
    uint8_t pending = 0;
    (void)budget;
    if (c == NULL || !c->claimed) return;
    m = c->mapping;
    {
        tiku_ipc_err_t result = session(c, endpoint);
        if (result != TIKU_IPC_OK) {
            for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) {
                if (!c->busy[i]) continue;
                pending = 1;
                if (result == TIKU_IPC_DOWN && !c->orphan[i]) {
                    (void)tiku_ipc_complete(c->active[i].submission,
                                            c->active[i].session, TIKU_IPC_DOWN);
                    c->orphan[i] = 1;
                }
            }
            if (pending || c->control_pending || c->negotiating)
                (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
            return;
        }
    }
    if (c->control_pending) {
        m->invalidate(&m->ring->peer, sizeof m->ring->peer);
        m->barrier();
        if (m->ring->peer.epoch == c->epoch &&
            m->ring->peer.control_ack == c->control_sequence) {
            uint8_t data[64];
            uint32_t length = m->ring->peer.control_length;
            if (m->ring->peer.control_result || length > sizeof data ||
                ~tiku_ipc_shared_crc(UINT32_MAX, m->ring->peer.control_data,
                                      length) != m->ring->peer.control_crc) {
                c->control_pending = 0;
            } else {
                tiku_ipc_err_t result;
                for (n = 0; n < length; n++) data[n] = m->ring->peer.control_data[n];
                result = tiku_ipc_deliver(c->control_endpoint, c->control_session,
                                          data, (uint16_t)length);
                if (result != TIKU_IPC_AGAIN) c->control_pending = 0;
            }
        }
        if (c->control_pending) pending = 1;
    }
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) {
        tiku_ipc_shared_slot_t *slot = &m->ring->slots[i];
        tiku_ipc_err_t result = TIKU_IPC_OK;
        if (!c->busy[i]) continue;
        m->invalidate(&slot->reply, sizeof slot->reply);
        m->barrier();
        if (slot->reply.epoch != c->epoch || slot->reply.sequence != c->sequence[i]) {
            pending = 1;
            continue;
        }
        if (slot->reply.result || slot->reply.length != c->active[i].length)
            result = TIKU_IPC_IO;
        if (result == TIKU_IPC_OK &&
            c->active[i].direction == TIKU_IPC_OUTPUT && !c->orphan[i]) {
            m->invalidate((const void *)slot->payload, sizeof slot->payload);
            m->barrier();
            if (~tiku_ipc_shared_crc(UINT32_MAX, slot->payload,
                    c->active[i].length) != slot->reply.crc) result = TIKU_IPC_IO;
            else for (n = 0; n < c->active[i].length; n++)
                ((uint8_t *)c->active[i].data)[n] = slot->payload[n];
        }
        if (!c->orphan[i])
            (void)tiku_ipc_complete(c->active[i].submission,
                                    c->active[i].session, result);
        c->busy[i] = 0;
    }
    if (pending) (void)tiku_ipc_service_at(endpoint, m->poll_ticks);
    m->invalidate(&m->ring->peer, sizeof m->ring->peer);
    m->barrier();
    if (m->ring->peer.epoch == c->epoch) {
        c->observed_bytes = m->ring->peer.bytes;
        c->observed_crc = m->ring->peer.crc;
        c->observed_max_gap = m->ring->peer.service_max_gap;
        c->observed_calls = m->ring->peer.service_calls;
    }
}

static void status(void *context, tiku_ipc_status_t *out)
{
    tiku_ipc_shared_context_t *c = context;
    unsigned i;
    if (c == NULL || !c->claimed) return;
    out->transport_stats = 1;
    out->transport_bytes = c->observed_bytes;
    out->transport_crc = c->observed_crc;
    out->peer_service_max_gap = c->observed_max_gap;
    out->peer_service_calls = c->observed_calls;
    out->transport_inflight = c->control_pending;
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++)
        if (c->busy[i]) out->transport_inflight++;
}

static uint8_t quiesce(void *context, uint16_t endpoint,
                       const tiku_ipc_transfer_t *transfer)
{
    tiku_ipc_shared_context_t *c = context;
    unsigned i;
    (void)endpoint;
    if (c == NULL || !c->claimed) return 0;
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) {
        if (c->busy[i] && c->active[i].submission.generation ==
                transfer->submission.generation &&
            c->active[i].submission.slot_plus_one == transfer->submission.slot_plus_one &&
            c->active[i].session == transfer->session) {
            /* The peer sees a fixed-slot copy; it cannot reach private backing.
             * Drop future copy-out before allowing that backing to return. */
            c->orphan[i] = 1;
            return 1;
        }
    }
    return 1;
}

/** @brief Invalidate the connection epoch while fencing staged copy-out. */
static tiku_ipc_err_t reset(void *context, uint16_t endpoint)
{
    tiku_ipc_shared_context_t *c = context;
    unsigned i;
    if (!c || !c->claimed || c->retired) return TIKU_IPC_DOWN;
    c->control_pending = 0;
    for (i = 0; i < TIKU_IPC_SHARED_SLOTS; i++) c->orphan[i] = 1;
    c->mapping->ring->host.magic = 0;
    c->mapping->clean(&c->mapping->ring->host, sizeof c->mapping->ring->host);
    c->mapping->barrier();
    (void)tiku_ipc_service_at(endpoint, 0);
    return TIKU_IPC_OK;
}

const tiku_ipc_backend_ops_t tiku_ipc_shared_ops = {
    .send = send, .start = start, .progress = progress, .quiesce = quiesce,
    .max_message = 64, .max_buffer = TIKU_IPC_SHARED_PAYLOAD, .status = status,
    .reset = reset, .reset_scope = TIKU_IPC_RESET_CONNECTION
};
#endif
