/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_coproc_ipc.h - native coprocessor operation numbers and boot selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COPROC_IPC_H_
#define TIKU_COPROC_IPC_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#if TIKU_IPC_ENABLE
/** @brief Native compatibility operations preserve the driver's slot semantics. */
enum { TIKU_COPROC_CTL_FLAGS, TIKU_COPROC_CTL_STATE, TIKU_COPROC_CTL_START,
    TIKU_COPROC_CTL_STOP, TIKU_COPROC_CTL_ALIVE, TIKU_COPROC_CTL_HEARTBEAT,
    TIKU_COPROC_CTL_IMAGE, TIKU_COPROC_CTL_SEND, TIKU_COPROC_CTL_POLL,
    TIKU_COPROC_CTL_SEQUENCE, TIKU_COPROC_CTL_REPLY };
/** @brief Selected during boot; UINT16_MAX without a compatibility endpoint. */
extern uint16_t tiku_coproc_ipc_endpoint;
#endif
#endif
