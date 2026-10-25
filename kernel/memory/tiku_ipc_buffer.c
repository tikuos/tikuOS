/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_buffer.c - private TRANSIENT pool with retained loan backing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_buffer.h"
#include "tiku_mem_internal.h"
#include <kernel/ipc/tiku_ipc_internal.h>
#include <hal/tiku_cpu.h>
#include <string.h>

#if TIKU_IPC_ENABLE
#if TIKU_IPC_BUFFERS_MAX < 1 || TIKU_IPC_BUFFERS_MAX > 65535
#error Invalid IPC buffer table size
#endif

static tiku_pool_t pool;
static uint8_t initialized;
static struct {
    struct tiku_process *owner;
    uint32_t instance, generation;
    void *data;
    uint16_t length;
    uint8_t caller, loan, valid, quarantined;
} buffers[TIKU_IPC_BUFFERS_MAX];

void tiku_ipc_buffer_init(void) { initialized = 1; }

static int buffer_index(tiku_ipc_buffer_t h)
{
    unsigned i;
    if (!h.slot_plus_one || h.slot_plus_one > TIKU_IPC_BUFFERS_MAX ||
        (h.generation & 3u) != 2u) return tiku_ipc_reject_handle();
    i = h.slot_plus_one - 1u;
    return buffers[i].data != NULL && buffers[i].generation == h.generation
           ? (int)i : tiku_ipc_reject_handle();
}

static int caller_owns(unsigned i)
{
    return TIKU_THIS() != NULL && TIKU_THIS()->is_running &&
        buffers[i].caller && buffers[i].owner == TIKU_THIS() &&
        buffers[i].instance == TIKU_THIS()->instance;
}

static void collect(unsigned i)
{
    if (buffers[i].data == NULL || buffers[i].caller || buffers[i].loan ||
        buffers[i].quarantined) return;
    (void)tiku_pool_free(&pool, buffers[i].data);
    (void)tiku_backing_pin(&pool, pool.backing, TIKU_BACKING_POOL, 0);
    buffers[i].data = NULL;
    buffers[i].owner = NULL;
}

tiku_ipc_err_t tiku_ipc_buffer_alloc(uint16_t length, tiku_ipc_buffer_t *out)
{
    unsigned i;
    tiku_mem_request_t request = TIKU_MEM_REQUEST_DEFAULT;
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (!initialized || out == NULL || !length || length > TIKU_IPC_BUFFER_SIZE ||
        TIKU_THIS() == NULL || !TIKU_THIS()->is_running) return TIKU_IPC_INVALID;
    for (i = 0; i < TIKU_IPC_BUFFERS_MAX; i++)
        if (buffers[i].data == NULL &&
            (buffers[i].generation >> 2) < TIKU_IPC_GENERATION_MAX) break;
    if (i == TIKU_IPC_BUFFERS_MAX) return TIKU_IPC_AGAIN;
    if (!tiku_backing_valid(&pool, pool.backing, TIKU_BACKING_POOL)) {
        pool = (tiku_pool_t){0};
        request.allocation_class = TIKU_MEM_TRANSIENT;
        if (tiku_mem_pool_create(&pool, TIKU_IPC_BUFFER_SIZE,
                TIKU_IPC_BUFFERS_MAX, &request) != TIKU_MEM_OK)
            return TIKU_IPC_AGAIN;
    }
    buffers[i].data = tiku_pool_alloc(&pool);
    if (buffers[i].data == NULL) return TIKU_IPC_AGAIN;
    if (tiku_backing_pin(&pool, pool.backing, TIKU_BACKING_POOL, 1) != TIKU_MEM_OK) {
        (void)tiku_pool_free(&pool, buffers[i].data);
        buffers[i].data = NULL;
        return TIKU_IPC_BUSY;
    }
    buffers[i].generation = ((buffers[i].generation >> 2) + 1u) * 4u + 2u;
    buffers[i].owner = TIKU_THIS();
    buffers[i].instance = TIKU_THIS()->instance;
    buffers[i].length = length;
    buffers[i].caller = buffers[i].valid = 1;
    buffers[i].loan = buffers[i].quarantined = 0;
    memset(buffers[i].data, 0, length);
    *out = (tiku_ipc_buffer_t){buffers[i].generation, (uint16_t)(i + 1u)};
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_buffer_map(tiku_ipc_buffer_t h,
                                  void **data, uint16_t *length)
{
    int i = buffer_index(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (i < 0 || data == NULL || length == NULL || !caller_owns((unsigned)i))
        return TIKU_IPC_INVALID;
    if (buffers[i].loan || buffers[i].quarantined || !buffers[i].valid)
        return TIKU_IPC_BUSY;
    *data = buffers[i].data;
    *length = buffers[i].length;
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_buffer_release(tiku_ipc_buffer_t h)
{
    int i = buffer_index(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (i < 0 || !caller_owns((unsigned)i)) return TIKU_IPC_INVALID;
    buffers[i].caller = 0;
    collect((unsigned)i);
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_ipc_buffer_loan(tiku_ipc_buffer_t h,
    tiku_ipc_direction_t direction, uint16_t limit, void **data, uint16_t *length)
{
    int i = buffer_index(h);
    TIKU_MEM_KERNEL_ONLY(TIKU_IPC_INVALID);
    if (i < 0 || !caller_owns((unsigned)i) || data == NULL || length == NULL ||
        (direction != TIKU_IPC_INPUT && direction != TIKU_IPC_OUTPUT))
        return TIKU_IPC_INVALID;
    if (buffers[i].length > limit) return TIKU_IPC_INVALID;
    if (buffers[i].loan || buffers[i].quarantined ||
        (direction == TIKU_IPC_INPUT && !buffers[i].valid)) return TIKU_IPC_BUSY;
    buffers[i].loan = (uint8_t)direction + 1u;
    if (direction == TIKU_IPC_OUTPUT) buffers[i].valid = 0;
    *data = buffers[i].data;
    *length = buffers[i].length;
    return TIKU_IPC_OK;
}

void tiku_ipc_buffer_return(tiku_ipc_buffer_t h, tiku_ipc_err_t status)
{
    int i = buffer_index(h);
    if (i < 0 || !buffers[i].loan) return;
    if (buffers[i].loan == (uint8_t)TIKU_IPC_OUTPUT + 1u)
        buffers[i].valid = status == TIKU_IPC_OK;
    buffers[i].loan = buffers[i].quarantined = 0;
    collect((unsigned)i);
}

void tiku_ipc_buffer_quarantine(tiku_ipc_buffer_t h)
{
    int i = buffer_index(h);
    if (i >= 0 && buffers[i].loan) buffers[i].quarantined = 1;
}

void tiku_ipc_buffer_owner_exit(struct tiku_process *p, uint32_t instance)
{
    unsigned i;
    for (i = 0; i < TIKU_IPC_BUFFERS_MAX; i++) {
        if (buffers[i].owner == p && buffers[i].instance == instance) {
            buffers[i].caller = 0;
        }
    }
}

void tiku_ipc_buffer_counts(uint16_t *live, uint16_t *free_count,
                             uint16_t *quarantined)
{
    unsigned i;
    *live = *free_count = *quarantined = 0;
    for (i = 0; i < TIKU_IPC_BUFFERS_MAX; i++) {
        if (buffers[i].quarantined) (*quarantined)++;
        else if (buffers[i].data != NULL) (*live)++;
        else if ((buffers[i].generation >> 2) < TIKU_IPC_GENERATION_MAX)
            (*free_count)++;
    }
}

void tiku_ipc_buffer_collect(void)
{
    unsigned i;
    for (i = 0; i < TIKU_IPC_BUFFERS_MAX; i++) {
        if (buffers[i].owner != NULL &&
            buffers[i].owner->instance != buffers[i].instance)
            buffers[i].caller = 0;
        collect(i);
    }
}
#endif
