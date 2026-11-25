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

/*---------------------------------------------------------------------------*/
/* SPEED                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Note the medium's speed: every session paces to it, and the watch
 *         starts afresh. */
static void
wire_rate(tiku_peer_wire_t *w, uint32_t bits)
{
    uint32_t rate = w->byte_rate;
    uint8_t i;

    if (w->speed >= 100u) {
        rate = (bits / 100u) * w->byte_rate / (w->speed / 100u);
    }
    w->now_speed = bits;
    for (i = 0; i < w->peer->service_count; i++) {
        tiku_ipc_wire_rate(&w->wire[i], rate);
    }
    w->heard_at = (w->board->heard != 0) ? w->board->heard() : 0u;
    w->clean_since = tiku_clock_time();
}

/** @brief Back to the starting speed, the sessions renewed, so a peer at
 *         another speed hears its hellos garbled and comes back too.
 *  @return 0, or 1 while the medium is still sending */
static int
wire_fall_back(tiku_peer_wire_t *w)
{
    int rc = w->board->speed(w->speed, 1u);

    if (rc != 0) {
        return rc;
    }
    wire_rate(w, w->speed);
    wire_renew(w);
    if (w->fallbacks != 0xFFFFu) {
        w->fallbacks++;
    }
    return 0;
}

/**
 * @brief After a pass over the medium, away from the starting speed: bytes
 *        heard outside whole frames for TIKU_PEER_WIRE_GUARD_S mean the peer
 *        talks at another speed, such as a restarted peer at its first one.
 */
static void
wire_watch(tiku_peer_wire_t *w)
{
    uint32_t heard;
    tiku_clock_time_t now = tiku_clock_time();

    if (w->now_speed == w->speed || w->board->heard == 0) {
        return;
    }
    heard = w->board->heard();
    if (w->framed != 0u || heard == w->heard_at) {
        w->framed = 0u;
        w->heard_at = heard;
        w->clean_since = now;
        return;
    }
    if (heard - w->heard_at >= TIKU_PEER_WIRE_GUARD_BYTES &&
        (tiku_clock_time_t)(now - w->clean_since) >=
        (tiku_clock_time_t)(TIKU_PEER_WIRE_GUARD_S * TIKU_CLOCK_SECOND)) {
        (void)wire_fall_back(w);
    }
}

/** @brief The medium's send, as the channels use it. */
static int
watch_send(tiku_link_t *l, const void *head, size_t hlen, const void *body,
           size_t blen)
{
    return tiku_link_send2(((tiku_peer_wire_t *)l->ctx)->medium, head, hlen,
                           body, blen);
}

/** @brief A pass over the medium, then the watch on what it heard. */
static void
watch_pump(tiku_link_t *l)
{
    tiku_peer_wire_t *w = l->ctx;

    tiku_link_pump(w->medium);
    wire_watch(w);
}

/** @brief Close the medium. */
static void
watch_close(tiku_link_t *l)
{
    tiku_link_close(((tiku_peer_wire_t *)l->ctx)->medium);
}

/** @brief A whole frame from the medium, passed to the channels. */
static void
watch_recv(void *context, uint8_t *msg, size_t len)
{
    tiku_peer_wire_t *w = context;

    w->framed = 1u;
    tiku_link_deliver(&w->watch, msg, len);
}

static const tiku_link_ops_t watch_ops = { watch_send, watch_pump, watch_close };

/** @brief Put the medium at the starting speed for a peer that is about to
 *         start at it; a medium still sending is caught by the watch. */
static void
wire_first_speed(tiku_peer_wire_t *w)
{
    if (w->board->speed != 0 && w->speed != 0u && w->now_speed != w->speed &&
        w->board->speed(w->speed, 1u) == 0) {
        wire_rate(w, w->speed);
    }
}

/** @brief The driver's speed call (tiku_peer_ops_t). */
static int
wire_speed(void *context, uint32_t bits, uint8_t what)
{
    tiku_peer_wire_t *w = context;
    int rc;

    if (w->bound == 0u || w->board->speed == 0 || w->speed == 0u) {
        return -1;
    }
    if (what == TIKU_PEER_SPEED_START) {
        return wire_fall_back(w);
    }
    rc = w->board->speed(bits, (what == TIKU_PEER_SPEED_SET) ? 1u : 0u);
    if (rc == 0 && what == TIKU_PEER_SPEED_SET) {
        wire_rate(w, bits);
    }
    return rc;
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
    if (physical == (tiku_link_t *)0) {
        return -1;
    }
    w->medium = physical;
    w->watch = (tiku_link_t){ &watch_ops, w, physical->cap, 0, 0 };
    tiku_link_on_recv(physical, watch_recv, w);
    if (tiku_ipc_mux_init(&w->mux, &w->watch) != 0 ||
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
    }
    wire_rate(w, w->speed);
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
    wire_first_speed(w);
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
    wire_first_speed(w);
    return (w->board->power(0u) == 0) ? 0 : -1;
}

/** @brief Reset the peer; its new instance says hello with a new epoch. */
static int
wire_reset(void *context)
{
    tiku_peer_wire_t *w = context;

    if (w->bound == 0u || w->board->reset == 0) {
        return -1;
    }
    wire_first_speed(w);
    if (w->board->reset() != 0) {
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
    out->speed = (w->board->speed != 0) ? w->now_speed : 0u;
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
    0, wire_bind, wire_start, wire_stop, wire_reset, wire_status, wire_speed
};

#endif /* TIKU_PEER_ENABLE */
