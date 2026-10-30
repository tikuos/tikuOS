/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_bus.c - packet ownership through completion and abort.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_bus.h"
#include <string.h>
#if (TIKU_DRV_WIFI_IPC_ENABLE + 0) || (TIKU_C6_RADIO_SERVICE_ENABLE + 0)
#include <interfaces/wireless/tiku_wireless_ipc.h>
#define RADIO_SCRUB(p,n) tiku_radio_wipe(p,n)
#else
#define RADIO_SCRUB(p,n) ((void)0)
#endif

#if TIKU_IPC_ENABLE
/** @brief Copy accepted frames behind a two-byte length in private staging. */
static int send(tiku_link_t *link, const void *head, size_t hn,
                  const void *body, size_t bn)
{
    tiku_ipc_bus_t *c = link->ctx;
    size_t length;
    if (hn > 256 || bn > 256-hn || c->queued || c->closing ||
        c->quarantined || (hn && !head) || (bn && !body)) return -1;
    length = hn+bn;
    if (!length) return -1;
    memset(c->pending,0,sizeof c->pending);
    c->pending[0] = (uint8_t)length; c->pending[1] = (uint8_t)(length >> 8);
    if (hn) memcpy(c->pending+2,head,hn);
    if (bn) memcpy(c->pending+2+hn,body,bn);
    c->queued = 1;
    return 0;
}
/** @brief Complete an exchange before permitting stage reuse or delivery. */
static void pump(tiku_link_t *link)
{
    tiku_ipc_bus_t *c = link->ctx;
    tiku_ipc_err_t result;
    if (c->active) {
        result = c->io->poll(c->io->context);
        if(result==TIKU_IPC_AGAIN&&c->io->listening&&c->io->listening(c->io->context))
            c->deadline=(tiku_clock_time_t)(tiku_clock_time()+c->io->burst_ticks);
        if (result == TIKU_IPC_AGAIN && TIKU_CLOCK_LT(tiku_clock_time(),c->deadline)) return;
        if (result != TIKU_IPC_OK) {
            c->failed++;
            if (!c->io->abort(c->io->context)) { c->quarantined = 1; return; }
        }
        c->active = c->quarantined = 0;
        if (result == TIKU_IPC_OK && !c->closing) {
            uint16_t length = (uint16_t)(c->rx[0] | (uint16_t)c->rx[1] << 8);
            if (length && length <= 256) tiku_link_deliver(link,c->rx+2,length);
            else if (length) c->failed++;
        }
    }
    if (c->closing) {
        RADIO_SCRUB(c->tx,sizeof c->tx);RADIO_SCRUB(c->rx,sizeof c->rx);return;
    }
    memset(c->tx,0,sizeof c->tx);
    if (c->queued) memcpy(c->tx,c->pending,sizeof c->pending);
    memset(c->rx,0,sizeof c->rx);
    result = c->io->start(c->io->context,c->tx,c->rx,sizeof c->tx);
    if (result == TIKU_IPC_OK) {
        c->queued = 0;
        RADIO_SCRUB(c->pending,sizeof c->pending);
        c->active = 1;
        c->deadline = (tiku_clock_time_t)(tiku_clock_time()+c->io->burst_ticks);
    } else if (result != TIKU_IPC_AGAIN) c->failed++;
}
/** @brief Stop new traffic; an unconfirmed abort keeps the staging reserved. */
static void close(tiku_link_t *link)
{
    tiku_ipc_bus_t *c = link->ctx;
    c->closing = 1;
    RADIO_SCRUB(c->pending,sizeof c->pending);
    c->queued = 0;
    tiku_link_on_recv(link,NULL,NULL);
    if (c->active) {
        if (c->io->abort(c->io->context)) c->active = c->quarantined = 0;
        else c->quarantined = 1;
    }
    if (!c->active) {
        RADIO_SCRUB(c->tx,sizeof c->tx);
        RADIO_SCRUB(c->rx,sizeof c->rx);
    }
}
static const tiku_link_ops_t ops = {.send=send,.pump=pump,.close=close};

tiku_link_t *tiku_ipc_bus_open(tiku_ipc_bus_t *c, const tiku_ipc_packet_io_t *io)
{
    if (!c || c->initialized || !io || !io->start || !io->poll || !io->abort ||
        !io->burst_ticks || io->burst_ticks > TIKU_CLOCK_MAX_INTERVAL ||
        tiku_ipc_domain_check(io->domain,io->engine,io->peer,c->tx,sizeof c->tx,
                             TIKU_IPC_INPUT) != TIKU_IPC_OK ||
        tiku_ipc_domain_check(io->domain,io->engine,io->peer,c->rx,sizeof c->rx,
                             TIKU_IPC_OUTPUT) != TIKU_IPC_OK) return NULL;
    c->io = io;
    c->link = (tiku_link_t){.ops=&ops,.ctx=c};
    c->initialized = 1;
    return &c->link;
}
uint8_t tiku_ipc_bus_reusable(const tiku_ipc_bus_t *c)
{ return c && c->initialized && c->closing && !c->active && !c->quarantined; }
#endif
