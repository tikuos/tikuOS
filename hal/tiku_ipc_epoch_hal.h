/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_epoch_hal.h - entropy-backed connection identities.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_EPOCH_HAL_H_
#define TIKU_IPC_EPOCH_HAL_H_
#include <kernel/ipc/tiku_ipc.h>
#if TIKU_IPC_ENABLE
/** @brief Obtain a nonzero 64-bit epoch; entropy failure has no time fallback. */
tiku_ipc_err_t tiku_ipc_epoch_fresh(uint64_t *out);
#endif
#endif
