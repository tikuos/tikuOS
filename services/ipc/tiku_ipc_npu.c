/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_npu.c - timed accelerator progress and buffer ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_npu.h"
#include <string.h>
#if TIKU_IPC_ENABLE
#define JOB_TICKS ((TIKU_CLOCK_SECOND + 19u) / 20u)
#define ABORT_TICKS 4u

/** @brief Encode one unsigned little-endian word. */
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8u));
}

/** @brief Validate and snapshot the engine-owned tensor intervals. */
static tiku_ipc_err_t spans(tiku_ipc_npu_context_t *c)
{
    tiku_ipc_err_t result;
    if (!c || !c->ops || !c->ops->spans || !c->ops->begin ||
        !c->ops->poll || !c->ops->abort) return TIKU_IPC_INVALID;
    if (tiku_ipc_npu_busy(c)) return TIKU_IPC_AGAIN;
    result = c->ops->spans(c->engine, &c->spans);
    if (result != TIKU_IPC_OK) return result;
    if (!c->spans.input || !c->spans.output || !c->spans.input_bytes ||
        !c->spans.output_bytes || !c->spans.generation) return TIKU_IPC_INVALID;
    return TIKU_IPC_OK;
}

/** @brief Begin one job after the driver has validated its staged tensors. */
static tiku_ipc_err_t begin(tiku_ipc_npu_context_t *c, uint16_t endpoint,
                             uint32_t session)
{
    tiku_ipc_err_t result = c->ops->begin(c->engine);
    if (result != TIKU_IPC_OK) return result;
    c->endpoint = endpoint;
    c->session = session;
    c->running = 1;
    c->orphan = c->aborting = 0;
    c->deadline = (tiku_clock_time_t)(tiku_clock_time() + JOB_TICKS);
    (void)tiku_ipc_service_at(endpoint, 1);
    return TIKU_IPC_OK;
}

/** @brief Run or cancel the legacy engine-buffer job using an eight-byte command. */
static tiku_ipc_err_t send(void *context, uint16_t endpoint, uint32_t session,
                            const void *data, uint16_t length)
{
    tiku_ipc_npu_context_t *c = context;
    const uint8_t *p = data;
    uint32_t token = 0;
    tiku_ipc_err_t result;
    if (!p || length != 8 || p[1] != 'N' || p[2] != 'P' || p[3] != 1)
        return TIKU_IPC_INVALID;
    for (unsigned i = 0; i < 4; i++) token |= (uint32_t)p[4+i] << (i*8u);
    if (!token || (p[0] != 1 && p[0] != 2)) return TIKU_IPC_INVALID;
    if (p[0] == 2) {
        if (!c || c->buffered || c->endpoint != endpoint || c->session != session ||
            c->token != token || !c->running) return TIKU_IPC_INVALID;
        c->result = TIKU_IPC_CANCELLED;
        c->aborting = 1;
        c->abort_deadline = (tiku_clock_time_t)(tiku_clock_time() + ABORT_TICKS);
        (void)tiku_ipc_service_at(endpoint, 0);
        return TIKU_IPC_OK;
    }
    result = spans(c);
    if (result != TIKU_IPC_OK) return result;
    c->buffered = 0;
    c->token = token;
    c->staged = 0;
    return begin(c, endpoint, session);
}

/** @brief Stage a complete small input, or run into a managed output loan. */
static tiku_ipc_err_t start(void *context, uint16_t endpoint,
                             const tiku_ipc_transfer_t *transfer)
{
    tiku_ipc_npu_context_t *c = context;
    tiku_ipc_err_t result = spans(c);
    if (result != TIKU_IPC_OK) return result;
    if (transfer->direction == TIKU_IPC_INPUT) {
        if (transfer->length != c->spans.input_bytes) return TIKU_IPC_INVALID;
        memcpy(c->spans.input, transfer->data, transfer->length);
        c->staged = 1;
        c->staged_session = transfer->session;
        c->staged_generation = c->spans.generation;
        return tiku_ipc_complete(transfer->submission, transfer->session, TIKU_IPC_OK);
    }
    if (transfer->length != c->spans.output_bytes || !c->staged ||
        c->staged_session != transfer->session ||
        c->staged_generation != c->spans.generation) return TIKU_IPC_INVALID;
    c->staged = 0;
    c->buffered = 1;
    c->transfer = *transfer;
    return begin(c, endpoint, transfer->session);
}

/** @brief Publish an outcome independently of retained engine ownership. */
static void flush(tiku_ipc_npu_context_t *c, uint16_t endpoint)
{
    tiku_ipc_err_t result;
    if (c->reply_pending) {
        uint8_t response[8];
        put32(response, c->token);
        put32(response + 4, (uint32_t)(int32_t)c->result);
        result = tiku_ipc_deliver(c->endpoint, c->session, response, sizeof response);
        if (result == TIKU_IPC_AGAIN) (void)tiku_ipc_service_at(endpoint, 1);
        else c->reply_pending = 0;
    }
}

/** @brief Retain engine backing until completion or a confirmed abort. */
static void progress(void *context, uint16_t endpoint, uint16_t budget)
{
    tiku_ipc_npu_context_t *c = context;
    tiku_ipc_status_t status;
    tiku_ipc_err_t result;
    (void)budget;
    if (!c) return;
    if (c->blocked) { flush(c, endpoint); return; }
    if (c->running) {
        if (tiku_ipc_status(c->endpoint, &status) != TIKU_IPC_OK || !status.open ||
            status.session != c->session) {
            c->orphan = 1;
            if (!c->aborting) {
                c->aborting = 1;
                c->result = TIKU_IPC_CANCELLED;
                c->abort_deadline = (tiku_clock_time_t)(tiku_clock_time() + ABORT_TICKS);
            }
        }
        result = c->aborting ? c->result : c->ops->poll(c->engine);
        if (!c->aborting && result == TIKU_IPC_AGAIN &&
            !TIKU_CLOCK_LT(tiku_clock_time(), c->deadline)) result = TIKU_IPC_TIMEOUT;
        if (result != TIKU_IPC_OK && result != TIKU_IPC_AGAIN) {
            if (!c->aborting) {
                c->aborting = 1;
                c->result = result;
                c->abort_deadline = (tiku_clock_time_t)(tiku_clock_time() + ABORT_TICKS);
            }
            if (!c->ops->abort(c->engine)) {
                if (!TIKU_CLOCK_LT(tiku_clock_time(), c->abort_deadline)) {
                    c->blocked = 1;
                    if (!c->buffered && !c->orphan) {
                        c->reply_pending = 1;
                        flush(c, endpoint);
                    }
                }
                else (void)tiku_ipc_service_at(endpoint, 1);
                return;
            }
            result = c->result;
        }
        if (result == TIKU_IPC_AGAIN) { (void)tiku_ipc_service_at(endpoint, 1); return; }
        c->running = 0;
        c->aborting = 0;
        c->result = result;
        if (c->buffered) {
            if (result == TIKU_IPC_OK && !c->orphan)
                memcpy(c->transfer.data, c->spans.output, c->transfer.length);
            (void)tiku_ipc_complete(c->transfer.submission, c->transfer.session,
                c->orphan ? TIKU_IPC_CANCELLED : result);
        } else if (!c->orphan) c->reply_pending = 1;
    }
    flush(c, endpoint);
}

/** @brief Fence private copy-out and attest the engine's final access. */
static uint8_t quiesce(void *context, uint16_t endpoint,
                        const tiku_ipc_transfer_t *transfer)
{
    tiku_ipc_npu_context_t *c = context;
    (void)endpoint;
    if (!c->running || c->transfer.submission.generation != transfer->submission.generation ||
        c->transfer.submission.slot_plus_one != transfer->submission.slot_plus_one) return 1;
    c->orphan = 1;
    if (c->ops->abort(c->engine)) { c->running = c->blocked = 0; return 1; }
    c->result = TIKU_IPC_CANCELLED;
    c->aborting = 1;
    c->abort_deadline = (tiku_clock_time_t)(tiku_clock_time() + ABORT_TICKS);
    (void)tiku_ipc_service_at(c->endpoint, 1);
    return 0;
}

uint8_t tiku_ipc_npu_busy(const tiku_ipc_npu_context_t *c)
{
    return c && (c->running || c->reply_pending || c->blocked);
}

tiku_ipc_err_t tiku_ipc_npu_stop(tiku_ipc_npu_context_t *c)
{
    if (!c || !c->ops) return TIKU_IPC_INVALID;
    c->staged = 0;
    if (!c->running && !c->blocked) { c->reply_pending = 0; return TIKU_IPC_OK; }
    if (!c->ops->abort(c->engine)) { c->blocked = 1; return TIKU_IPC_BUSY; }
    if (c->buffered)
        (void)tiku_ipc_complete(c->transfer.submission, c->transfer.session, TIKU_IPC_CANCELLED);
    c->running = c->blocked = c->reply_pending = c->aborting = 0;
    return TIKU_IPC_OK;
}

/** @brief Stop the engine and invalidate its endpoint sessions. */
static tiku_ipc_err_t reset(void *context, uint16_t endpoint)
{
    (void)endpoint;
    return tiku_ipc_npu_stop(context);
}

const tiku_ipc_backend_ops_t tiku_ipc_npu_ops = {
    .send = send, .start = start, .progress = progress, .quiesce = quiesce,
    .max_message = 8, .reset = reset, .reset_scope = TIKU_IPC_RESET_ENGINE
};
#endif
