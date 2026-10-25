/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_mux.c - bounded channel dispatch and shared-carrier isolation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_mux.h"
#include <string.h>
#if TIKU_IPC_ENABLE
/** @brief Copy the channel prefix and both message parts before carrier admission. */
static int send(tiku_link_t *link, const void *head, size_t hn, const void *body, size_t bn)
{
    tiku_ipc_mux_channel_t *c = link->ctx;
    uint8_t frame[256];
    if (!c->used || hn > 255 || bn > 255-hn || (hn && !head) || (bn && !body)) return -1;
    frame[0] = c->id;
    if (hn) memcpy(frame+1,head,hn);
    if (bn) memcpy(frame+1+hn,body,bn);
    return tiku_link_send(c->owner->physical,frame,hn+bn+1);
}
/** @brief Service the shared physical carrier from cooperative kernel context. */
static void pump(tiku_link_t *link)
{ tiku_link_pump(((tiku_ipc_mux_channel_t *)link->ctx)->owner->physical); }
/** @brief Remove a virtual receiver while keeping the physical carrier owned. */
static void close(tiku_link_t *link)
{
    tiku_ipc_mux_channel_t *c = link->ctx;
    c->used = 0; c->lost = NULL; c->lost_context = NULL;
    tiku_link_on_recv(link,NULL,NULL);
}
static const tiku_link_ops_t ops = {.send=send,.pump=pump,.close=close};
/** @brief Deliver a borrowed frame to its agreed channel; wire receivers copy it. */
static void receive(void *context, uint8_t *frame, size_t length)
{
    tiku_ipc_mux_t *c = context;
    if (!length || length > 256 || frame[0] >= TIKU_IPC_MUX_CHANNELS ||
        !c->channels[frame[0]].used) { c->dropped++; return; }
    tiku_link_deliver(&c->channels[frame[0]].link,frame+1,length-1);
}
int tiku_ipc_mux_init(tiku_ipc_mux_t *c, tiku_link_t *physical)
{
    if (!c || c->physical || !physical || !physical->ops) return -1;
    c->physical = physical;
    tiku_link_on_recv(physical,receive,c);
    return 0;
}
tiku_link_t *tiku_ipc_mux_open(tiku_ipc_mux_t *c, uint8_t id,
                               void (*lost)(void *), void *context)
{
    tiku_ipc_mux_channel_t *channel;
    if (!c || !c->physical || id >= TIKU_IPC_MUX_CHANNELS || c->channels[id].used) return NULL;
    channel = &c->channels[id];
    channel->owner = c; channel->id = id; channel->used = 1;
    channel->lost = lost; channel->lost_context = context;
    channel->link = (tiku_link_t){.ops=&ops,.ctx=channel,.cap=c->physical->cap};
    return &channel->link;
}
void tiku_ipc_mux_lost(tiku_ipc_mux_t *c)
{
    if (!c) return;
    for (unsigned i = 0; i < TIKU_IPC_MUX_CHANNELS; i++)
        if (c->channels[i].used && c->channels[i].lost)
            c->channels[i].lost(c->channels[i].lost_context);
}
#endif
