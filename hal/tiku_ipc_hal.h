/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_hal.h - fixed, DMA-free shared access-domain mappings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_HAL_H_
#define TIKU_IPC_HAL_H_
#include <hal/tiku_ipc_shared.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>

/** @brief Port-owned mapping; callbacks are bounded and never dispatch processes. */
typedef struct {
    tiku_ipc_shared_t *ring;
    const uint8_t *claim_base;
    tiku_mem_arch_size_t claim_size;
    tiku_mem_region_type_t region_type;
    void (*clean)(const void *address, uint32_t size);
    void (*invalidate)(const void *address, uint32_t size);
    void (*barrier)(void);
    uint8_t (*ready)(void);
    tiku_clock_time_t poll_ticks;
} tiku_ipc_shared_mapping_t;

/** @brief Fixed board mapping; implemented with the selected peer payload. */
const tiku_ipc_shared_mapping_t *tiku_ipc_hal_shared(void);
#endif
