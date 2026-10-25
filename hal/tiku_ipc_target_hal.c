/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_target_hal.c - bounded target byte handling and stage retention.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_target_hal.h"
#include <hal/tiku_cpu.h>
#if TIKU_IPC_ENABLE
tiku_ipc_err_t tiku_ipc_target_start(void *context, const void *tx, void *rx, uint16_t size)
{
    tiku_ipc_target_t *c = context;
    if (!c || !tx || !rx || !size || !c->quiesce) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    if (c->armed) { tiku_atomic_exit(); return TIKU_IPC_AGAIN; }
    c->tx = tx; c->rx = rx; c->capacity = size;
    c->sent = c->received = 0; c->fault = 0;
    c->armed = 1;
    tiku_atomic_exit();
    return TIKU_IPC_OK;
}
tiku_ipc_err_t tiku_ipc_target_poll(void *context)
{
    tiku_ipc_target_t *c = context;
    tiku_ipc_err_t result;
    if (!c) return TIKU_IPC_INVALID;
    tiku_atomic_enter();
    if (!c->armed) result = TIKU_IPC_INVALID;
    else if (c->fault) result = TIKU_IPC_IO;
    else if (c->sent != c->capacity || c->received != c->capacity) result = TIKU_IPC_AGAIN;
    else { c->armed = 0; c->tx = NULL; c->rx = NULL; result = TIKU_IPC_OK; }
    tiku_atomic_exit();
    return result;
}
uint8_t tiku_ipc_target_abort(void *context)
{
    tiku_ipc_target_t *c = context;
    if (!c || !c->quiesce || !c->quiesce(c->port)) return 0;
    tiku_atomic_enter();
    c->armed = 0; c->tx = NULL; c->rx = NULL;
    tiku_atomic_exit();
    return 1;
}
uint8_t tiku_ipc_target_write(tiku_ipc_target_t *c, uint8_t byte)
{
    if (!c || !c->armed || c->received >= c->capacity) {
        if (c && c->armed) c->fault = 1;
        return 0;
    }
    c->rx[c->received++] = byte;
    return 1;
}
uint8_t tiku_ipc_target_read(tiku_ipc_target_t *c)
{
    if (!c || !c->armed || c->sent >= c->capacity) {
        if (c && c->armed) c->fault = 1;
        return 0xff;
    }
    return c->tx[c->sent++];
}
uint8_t tiku_ipc_target_shift(tiku_ipc_target_t *c, uint8_t byte)
{
    if (!c || !c->spi) return 0xff;
    if (!tiku_ipc_target_write(c,byte)) return 0xff;
    return tiku_ipc_target_read(c);
}
void tiku_ipc_target_end(tiku_ipc_target_t *c)
{
    if (c && c->armed && (c->sent != c->capacity || c->received != c->capacity)) c->fault = 1;
}
#endif
