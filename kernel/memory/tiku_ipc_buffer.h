/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_buffer.h - private IPC backing and exclusive loan operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_BUFFER_H_
#define TIKU_IPC_BUFFER_H_
#include <kernel/ipc/tiku_ipc.h>

#if TIKU_IPC_ENABLE
/** @brief Enable the manager after boot installs the process-exit observer. */
void tiku_ipc_buffer_init(void);
/** @brief Kernel-only loan transitions; the IPC core validates the actor. */
tiku_ipc_err_t tiku_ipc_buffer_loan(tiku_ipc_buffer_t,
    tiku_ipc_direction_t, uint16_t limit, void **data, uint16_t *length);
void tiku_ipc_buffer_return(tiku_ipc_buffer_t, tiku_ipc_err_t status);
void tiku_ipc_buffer_quarantine(tiku_ipc_buffer_t);
/** @brief Mark caller references dropped; collection is deferred to progress. */
void tiku_ipc_buffer_owner_exit(struct tiku_process *, uint32_t instance);
/** @brief Collect dropped references and references belonging to exited owners. */
void tiku_ipc_buffer_collect(void);
/** @brief Count live, reusable and quarantined slots independently. */
void tiku_ipc_buffer_counts(uint16_t *live, uint16_t *free_count,
                             uint16_t *quarantined);
#endif
#endif
