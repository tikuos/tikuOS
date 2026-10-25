/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_npu.h - asynchronous accelerator endpoint over staged buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_NPU_H_
#define TIKU_IPC_NPU_H_
#include <kernel/ipc/tiku_ipc_backend.h>
#include <hal/tiku_npu_job_hal.h>
#if TIKU_IPC_ENABLE
/** @brief One engine instance; keep its context and operations for the boot. */
typedef struct {
    const tiku_npu_engine_ops_t *ops;
    void *engine;
    tiku_npu_tensor_spans_t spans;
    tiku_ipc_transfer_t transfer;
    tiku_clock_time_t deadline, abort_deadline;
    uint32_t session, token, staged_generation, staged_session;
    uint16_t endpoint;
    tiku_ipc_err_t result;
    uint8_t running, buffered, reply_pending, orphan, blocked, staged, aborting;
} tiku_ipc_npu_context_t;

extern const tiku_ipc_backend_ops_t tiku_ipc_npu_ops;
/** @brief True while engine spans or an unconsumed result remain reserved. */
uint8_t tiku_ipc_npu_busy(const tiku_ipc_npu_context_t *context);
/** @brief Stop a job; failed abort leaves the engine reserved. */
tiku_ipc_err_t tiku_ipc_npu_stop(tiku_ipc_npu_context_t *context);
#endif
#endif
