/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_sdio_packet.c - copy-out fencing across SDIO mode changes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_sdio_packet.h"
#if TIKU_IPC_ENABLE
/** @brief Advance recovery and signal a change of physical session or readiness. */
static void step(tiku_ipc_sdio_packet_t *c)
{
    uint8_t connected;
    tiku_ipc_sdio_step(c->owner);
    connected = c->owner->state == TIKU_SDIO_READY && c->owner->mode == TIKU_SDIO_NATIVE;
    if ((c->connected && !connected) || c->session != c->owner->session)
        if (c->lost) c->lost(c->lost_context);
    c->connected = connected;
    c->session = c->owner->session;
}
/** @brief Copy an admitted packet into the owner's reserved bulk queue. */
static tiku_ipc_err_t start(void *context, const void *tx, void *rx, uint16_t size)
{
    tiku_ipc_sdio_packet_t *c = context;
    tiku_ipc_err_t result;
    if (c->active) return TIKU_IPC_AGAIN;
    step(c);
    result = tiku_ipc_sdio_submit(c->owner,TIKU_SDIO_BULK,tx,size,size,
                                 c->io.burst_ticks,&c->job);
    if (result != TIKU_IPC_OK) return result;
    c->output = rx; c->length = size; c->active = 1;
    return TIKU_IPC_OK;
}
/** @brief Copy a response after the owner confirms final device access. */
static tiku_ipc_err_t poll(void *context)
{
    tiku_ipc_sdio_packet_t *c = context;
    tiku_sdio_result_t result;
    tiku_ipc_err_t status;
    step(c);
    if (!c->active) return TIKU_IPC_INVALID;
    status = tiku_ipc_sdio_take(c->owner,c->job,c->output,c->length,&result);
    if (status != TIKU_IPC_OK) return status;
    c->active = 0; c->output = NULL;
    return result.status;
}
/** @brief Drop future copy-out; the owner retains any engine-visible stages. */
static uint8_t abort_transfer(void *context)
{
    tiku_ipc_sdio_packet_t *c = context;
    if (c->active) (void)tiku_ipc_sdio_discard(c->owner,c->job);
    c->active = 0; c->output = NULL;
    return 1;
}
int tiku_ipc_sdio_packet_init(tiku_ipc_sdio_packet_t *c, tiku_ipc_sdio_t *owner,
    const tiku_ipc_access_domain_t *domain, uint32_t peer,
    void (*lost)(void *), void *context)
{
    if (!c || c->owner || !owner || !owner->claimed || !domain || domain->engine != 0 ||
        domain->peer != peer || owner->profile.burst > (TIKU_CLOCK_MAX_INTERVAL-4u)/4u)
        return -1;
    c->owner = owner; c->session = owner->session;
    c->connected = owner->state == TIKU_SDIO_READY && owner->mode == TIKU_SDIO_NATIVE;
    c->lost = lost; c->lost_context = context;
    c->io = (tiku_ipc_packet_io_t){start,poll,abort_transfer,c,domain,0,peer,
                                 (tiku_clock_time_t)(owner->profile.burst*4u+4u),NULL};
    return 0;
}
#endif
