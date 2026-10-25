/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_shared.h - copied staging through a claimed two-slot shared carve.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_SHARED_BACKEND_H_
#define TIKU_IPC_SHARED_BACKEND_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#include <kernel/memory/tiku_mem.h>
#include <hal/tiku_ipc_hal.h>

#if TIKU_IPC_ENABLE
/** @brief Static backend state; payload backing and handles are kept separately. */
typedef struct {
    const tiku_ipc_shared_mapping_t *mapping;
    tiku_ipc_transfer_t active[TIKU_IPC_SHARED_SLOTS];
    uint32_t sequence[TIKU_IPC_SHARED_SLOTS], epoch;
    uint32_t control_sequence, control_session;
    uint32_t observed_bytes, observed_crc;
    uint32_t observed_max_gap, observed_calls;
    uint16_t control_endpoint;
    uint8_t busy[TIKU_IPC_SHARED_SLOTS], orphan[TIKU_IPC_SHARED_SLOTS];
    uint8_t claimed, control_pending, negotiating, retired;
} tiku_ipc_shared_context_t;

/**
 * @brief Claim a port-declared carve at boot before writing any shared byte.
 *
 * The mapping must contain the ring, be aligned and lie in the stated region
 * type. The ring stays claimed for the boot. Arbitrary application memory is
 * not admitted. Claim owners are supplied by boot assembly.
 */
tiku_ipc_err_t tiku_ipc_shared_init(tiku_ipc_shared_context_t *context,
    const tiku_ipc_shared_mapping_t *mapping, uint8_t owner);
extern const tiku_ipc_backend_ops_t tiku_ipc_shared_ops;
#endif
#endif
