/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_packet_hal.c - access-domain admission before peripheral access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_packet_hal.h"
#if TIKU_IPC_ENABLE
tiku_ipc_err_t tiku_ipc_domain_check(const tiku_ipc_access_domain_t *d,
    uint32_t engine, uint32_t peer, const void *address, uintptr_t length,
    tiku_ipc_direction_t direction)
{
    uintptr_t start = (uintptr_t)address, offset;
    if (!d || !address || !length ||
        (direction != TIKU_IPC_INPUT && direction != TIKU_IPC_OUTPUT) ||
        d->engine != engine || d->peer != peer || !d->alignment ||
        (d->alignment & (d->alignment-1u)) ||
        (d->cache_line && (d->cache_line & (d->cache_line-1u))) ||
        !(d->directions & (direction == TIKU_IPC_INPUT ? 1u : 2u)) ||
        start < d->base || start % d->alignment ||
        (d->cache_line && (start % d->cache_line || length % d->cache_line)))
        return TIKU_IPC_INVALID;
    offset = start-d->base;
    if (offset > d->size || length > d->size-offset || length-1u > UINTPTR_MAX-start)
        return TIKU_IPC_INVALID;
    return TIKU_IPC_OK;
}
#endif
