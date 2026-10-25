/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_internal.h - rejected-handle accounting shared with managed memory.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_INTERNAL_H_
#define TIKU_IPC_INTERNAL_H_
#include "tiku_ipc_config.h"
#if TIKU_IPC_ENABLE
/** @brief IRQ-safe saturating rejected-handle count; return -1 to a lookup. */
int tiku_ipc_reject_handle(void);
#endif
#endif
