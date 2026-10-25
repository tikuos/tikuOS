/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_packet_hal.h - bounded packet transfers and engine-visible staging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_PACKET_HAL_H_
#define TIKU_IPC_PACKET_HAL_H_
#include <kernel/ipc/tiku_ipc.h>
#if TIKU_IPC_ENABLE
/** @brief Port-declared access interval for one peer and transfer engine. */
typedef struct {
    uintptr_t base, size;
    uint32_t engine, peer;
    uint16_t alignment, cache_line;
    uint8_t directions; /**< bit 0: engine reads; bit 1: engine writes. */
} tiku_ipc_access_domain_t;

/** @brief Refuse mismatched consumers, directions, bounds or cache-line sharing. */
tiku_ipc_err_t tiku_ipc_domain_check(const tiku_ipc_access_domain_t *domain,
    uint32_t engine, uint32_t peer, const void *address, uintptr_t length,
    tiku_ipc_direction_t direction);

/**
 * @brief One controller exchange or one armed target transaction.
 * @note OK from start retains both spans until poll or abort attests completion.
 * Other start results retain nothing. poll OK includes cache synchronization;
 * an error needs abort confirmation. No callback dispatches processes.
 */
typedef struct {
    tiku_ipc_err_t (*start)(void *, const void *tx, void *rx, uint16_t capacity);
    tiku_ipc_err_t (*poll)(void *);
    uint8_t (*abort)(void *);
    void *context;
    const tiku_ipc_access_domain_t *domain;
    uint32_t engine, peer;
    tiku_clock_time_t burst_ticks;
    uint8_t (*listening)(void *); /**< Optional: armed target awaiting observable controller progress. */
} tiku_ipc_packet_io_t;
#endif
#endif
