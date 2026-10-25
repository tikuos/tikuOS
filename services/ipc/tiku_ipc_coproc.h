/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_coproc.h - boot registration of native coprocessor controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_COPROC_H_
#define TIKU_IPC_COPROC_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#if TIKU_IPC_ENABLE
/** @brief Register the compatibility provider after portable IPC initialization. */
tiku_ipc_err_t tiku_ipc_coproc_init(uint16_t endpoint);
#endif
#endif
