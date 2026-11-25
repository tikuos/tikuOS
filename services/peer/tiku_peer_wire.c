/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_wire.c - a peer driver for a carrier of whole messages.
 *
 * The peer is READY while its first service's wire session is. The session
 * has no keep-alive: a peer that stops after READY is noticed when it says
 * hello again, or when this side's own sends go unanswered.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_peer_wire.h"
#include <hal/tiku_ipc_epoch_hal.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief 1 while the peer has power: always, seen from the worker side. */
static uint8_t
wire_powered(const tiku_peer_wire_t *w)
{
    return (w->board->powered != 0) ? w->board->powered() : 1u;
}

/** @brief Open or park the medium, once per change.  @return 0 or -1 */
static int
wire_carrier(tiku_peer_wire_t *w, uint8_t up)
{
    if (w->carrier_up == up) {
        return 0;
    }
    if (w->board->carrier != 0 && w->board->carrier(up) != 0) {
        return -1;
    }
    w->carrier_up = up;
    return 0;
}

/** @brief Restart every service's session: a new epoch, hello again. */
static void
wire_renew(tiku_peer_wire_t *w)
{
    uint8_t i;

    for (i = 0; i < w->peer->service_count; i++) {
        (void)tiku_ipc_reset((uint16_t)(w->first_endpoint + i));
    }
    w->waiting_since = tiku_clock_time();
}

/**
 * @brief Open the link, a channel per service and a wire session on each.
 *
 * The worker side brings its medium up here; a supervisor does at start,
 * unless the peer already has power.
 */
static int
wire_bind(void *context, uint16_t first_endpoint)
{
    tiku_peer_wire_t *w = context;
    const tiku_peer_t *p = w->peer;
    tiku_link_t *physical;
    uint64_t epoch;
    uint8_t i;

    if (w->bound != 0u || p->service_count == 0u ||
        p->service_count > TIKU_PEER_WIRE_SERVICES) {
        return -1;
    }
    physical = w->board->link();
    if (physical == (tiku_link_t *)0 ||
        tiku_ipc_mux_init(&w->mux, physical) != 0 ||
        tiku_ipc_epoch_fresh(&epoch) != TIKU_IPC_OK) {
        return -1;
    }
    /* An epoch per service, epoch + i: none is zero and none wraps. */
    if (epoch > UINT64_MAX - TIKU_PEER_WIRE_SERVICES) {
        epoch = 1u;
    }
    for (i = 0; i < p->service_count; i++) {
        tiku_link_t *l = tiku_ipc_mux_open(&w->mux, p->services[i].channel,
                                           0, 0);

        if (l == (tiku_link_t *)0 ||
            tiku_ipc_wire_init(&w->wire[i], l, p->services[i].name,
                               (uint16_t)(first_endpoint + i), epoch + i,
                               w->frame_size, 0) != TIKU_IPC_OK) {
            return -1;
        }
        tiku_ipc_wire_rate(&w->wire[i], w->byte_rate);
    }
    w->first_endpoint = first_endpoint;
    w->waiting_since = tiku_clock_time();
    w->bound = 1u;
    if (w->board->attend != 0) {
        w->board->attend(first_endpoint);
    }
    if (wire_powered(w) != 0u) {
        return wire_carrier(w, 1u);
    }
    return 0;
}

/** @brief Power the peer, bring the medium up and start the sessions anew. */
static int
wire_start(void *context)
{
    tiku_peer_wire_t *w = context;

    if (w->bound == 0u) {
        return -1;
    }
    if (w->board->power != 0 && w->board->power(1u) != 0) {
        return -1;
    }
    if (wire_carrier(w, 1u) != 0) {
        return -1;
    }
    wire_renew(w);
    return 0;
}

/** @brief End the sessions, park the medium, then take the peer's power. */
static int
wire_stop(void *context)
{
    tiku_peer_wire_t *w = context;

    if (w->bound == 0u || w->board->power == 0) {
        return -1;
    }
    wire_renew(w);
    (void)wire_carrier(w, 0u);
    return (w->board->power(0u) == 0) ? 0 : -1;
}

/** @brief Reset the peer; its new instance says hello with a new epoch. */
static int
wire_reset(void *context)
{
    tiku_peer_wire_t *w = context;

    if (w->bound == 0u || w->board->reset == 0 || w->board->reset() != 0) {
        return -1;
    }
    wire_renew(w);
    return 0;
}

/** @brief OFF without power; READY with the first session; else STARTING,
 *         and LOST once that has lasted start_ticks. */
static void
wire_status(void *context, tiku_peer_status_t *out)
{
    tiku_peer_wire_t *w = context;
    const tiku_ipc_wire_t *first = &w->wire[0];
    tiku_clock_time_t now = tiku_clock_time();

    out->powered = wire_powered(w);
    out->restarts = w->restarts;
    out->mode = 0u;
    out->why = TIKU_PEER_WHY_NONE;
    if (w->bound == 0u || out->powered == 0u) {
        out->state = TIKU_PEER_OFF;
        w->waiting_since = now;
        return;
    }
    if (first->ready != 0u) {
        if (first->peer_epoch != w->seen_epoch) {
            if (w->seen_epoch != 0u && w->restarts != 0xFFFFu) {
                w->restarts++;
                out->restarts = w->restarts;
            }
            w->seen_epoch = first->peer_epoch;
        }
        out->state = TIKU_PEER_READY;
        w->waiting_since = now;
        return;
    }
    if ((tiku_clock_time_t)(now - w->waiting_since) > w->start_ticks) {
        out->state = TIKU_PEER_LOST;
        out->why = (w->carrier_up != 0u) ? TIKU_PEER_WHY_TIMEOUT
                                         : TIKU_PEER_WHY_CARRIER;
        return;
    }
    out->state = TIKU_PEER_STARTING;
}

const tiku_peer_ops_t tiku_peer_wire_ops = {
    0, wire_bind, wire_start, wire_stop, wire_reset, wire_status
};

#endif /* TIKU_PEER_ENABLE */
