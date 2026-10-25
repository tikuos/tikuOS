/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_job_hal.h - staged accelerator jobs and confirmed aborts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_NPU_JOB_HAL_H_
#define TIKU_NPU_JOB_HAL_H_
#include <kernel/ipc/tiku_ipc.h>
#if TIKU_IPC_ENABLE
/** @brief Driver-owned, validated tensor spans pinned for a running job. */
typedef struct {
    void *input;
    const void *output;
    uint32_t input_bytes, output_bytes, generation;
} tiku_npu_tensor_spans_t;

/**
 * @brief Non-dispatching driver operations with bounded register waits.
 * @note poll returns AGAIN while running; OK includes output cache sync.
 * abort returns nonzero after final engine access; zero retains the spans.
 */
typedef struct {
    tiku_ipc_err_t (*spans)(void *, tiku_npu_tensor_spans_t *);
    tiku_ipc_err_t (*begin)(void *);
    tiku_ipc_err_t (*poll)(void *);
    uint8_t (*abort)(void *);
} tiku_npu_engine_ops_t;

/** @brief The selected port's engine; accesses driver buffers, not IPC pools. */
extern const tiku_npu_engine_ops_t tiku_npu_engine_ops;
#endif
#endif
