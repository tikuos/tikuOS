/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_arch.c - uncached LP SRAM mapping for the two-slot IPC backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_ipc_hal.h>
#include "lpcore/tiku_lpcore_ipc.h"
#include "tiku_lpcore_arch.h"

static void synchronize(const void *address, uint32_t size)
{
    (void)address; (void)size;
    __asm__ volatile ("fence" ::: "memory");
}

static void barrier(void) { __asm__ volatile ("fence" ::: "memory"); }
static uint8_t ready(void) { return tiku_p4_lpcore_running() != 0; }

static const tiku_ipc_shared_mapping_t mapping = {
    TIKU_LPCORE_STREAM, (const uint8_t *)TIKU_LPCORE_STREAM_ADDR,
    sizeof(tiku_ipc_shared_t), TIKU_MEM_REGION_SHARED,
    synchronize, synchronize, barrier, ready, 1
};

const tiku_ipc_shared_mapping_t *tiku_ipc_hal_shared(void) { return &mapping; }
