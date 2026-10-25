/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_async.c - process-owned NPU jobs and cooperative blocking runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_npu.h"
#include <tiku.h>
#include <kernel/scheduler/tiku_sched.h>
#include <kernel/cpu/tiku_hang.h>
#include <string.h>

#if TIKU_IPC_ENABLE
static uint32_t next_token;

/** @brief Build one versioned run/cancel command with an opaque job token. */
static void command(uint8_t *p, uint8_t op, uint32_t token)
{
    p[0] = op; p[1] = 'N'; p[2] = 'P'; p[3] = 1;
    for (unsigned i = 0; i < 4; i++) p[i+4] = (uint8_t)(token >> (i*8u));
}

/** @brief Decode one little-endian protocol word. */
static uint32_t word(const uint8_t *p)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) value |= (uint32_t)p[i] << (i*8u);
    return value;
}

/** @brief Map endpoint outcomes to the portable accelerator result set. */
static int outcome(tiku_ipc_err_t result)
{
    switch (result) {
    case TIKU_IPC_OK: return TIKU_NPU_OK;
    case TIKU_IPC_TIMEOUT: return TIKU_NPU_ERR_TIMEOUT;
    case TIKU_IPC_CANCELLED: return TIKU_NPU_ERR_CANCELLED;
    case TIKU_IPC_AGAIN: case TIKU_IPC_BUSY: return TIKU_NPU_ERR_BUSY;
    case TIKU_IPC_DOWN: case TIKU_IPC_INVALID: return TIKU_NPU_ERR_STATE;
    default: return TIKU_NPU_ERR_FAULT;
    }
}

int tiku_npu_submit(tiku_npu_job_t *job)
{
    uint8_t data[8];
    tiku_ipc_err_t result;
    tiku_ipc_status_t status;
    if (!job || job->endpoint.slot_plus_one) return TIKU_NPU_ERR_BUSY;
    if (next_token == UINT32_MAX) return TIKU_NPU_ERR_STATE;
    result = tiku_ipc_open("npu/inference", &job->endpoint);
    if (result != TIKU_IPC_OK) return outcome(result);
    job->token = ++next_token;
    command(data, 1, job->token);
    (void)tiku_ipc_query(job->endpoint, &status);
    job->session = status.session;
    result = tiku_ipc_send(job->endpoint, data, sizeof data);
    if (result != TIKU_IPC_OK) {
        (void)tiku_ipc_close(job->endpoint);
        memset(job, 0, sizeof *job);
        return outcome(result);
    }
    return TIKU_NPU_OK;
}

int tiku_npu_poll(tiku_npu_job_t *job)
{
    tiku_ipc_received_t reply;
    tiku_ipc_status_t status;
    tiku_ipc_err_t result;
    int rc;
    if (!job || !job->endpoint.slot_plus_one) return TIKU_NPU_ERR_STATE;
    if (tiku_ipc_query(job->endpoint, &status) != TIKU_IPC_OK)
        return TIKU_NPU_ERR_STATE;
    if (status.session != job->session) rc = TIKU_NPU_ERR_CANCELLED;
    else {
        result = tiku_ipc_receive(job->endpoint, &reply);
        if (result == TIKU_IPC_AGAIN) return TIKU_NPU_PENDING;
        if (result != TIKU_IPC_OK || reply.is_buffer || reply.length != 8 ||
            word(reply.data) != job->token) rc = TIKU_NPU_ERR_FAULT;
        else rc = outcome((tiku_ipc_err_t)(int32_t)word(reply.data + 4));
    }
    (void)tiku_ipc_close(job->endpoint);
    memset(job, 0, sizeof *job);
    return rc;
}

int tiku_npu_cancel(tiku_npu_job_t *job)
{
    uint8_t data[8];
    if (!job || !job->endpoint.slot_plus_one) return TIKU_NPU_ERR_STATE;
    command(data, 2, job->token);
    return outcome(tiku_ipc_send(job->endpoint, data, sizeof data));
}

int tiku_npu_wait_run(void)
{
    tiku_npu_job_t job = {0};
    struct tiku_process *owner = TIKU_THIS();
    int result = tiku_npu_submit(&job);
    if (result != TIKU_NPU_OK) return result;
    do {
        (void)tiku_sched_run_once_except(owner);
        tiku_watchdog_kick();
        tiku_hang_checkin();
        result = tiku_npu_poll(&job);
    } while (result == TIKU_NPU_PENDING);
    return result;
}
#endif
