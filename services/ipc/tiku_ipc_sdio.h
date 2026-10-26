/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_sdio.h - native, quiescing, reset and loader ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_SDIO_H_
#define TIKU_IPC_SDIO_H_
#include <hal/tiku_sdio_owner_hal.h>
#include <hal/tiku_sdio_diag.h>
#if TIKU_IPC_ENABLE
#define TIKU_SDIO_JOBS 4u
#define TIKU_SDIO_STAGE 512u
/** @brief Bulk burst and recovery bounds; control and loader jobs use their admitted timeout. */
typedef struct {
    tiku_clock_time_t burst, detect, abort, boot, negotiate;
} tiku_sdio_profile_t;
/** @brief Job identities retire on generation exhaustion. */
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_sdio_job_t;
/** @brief Connection transition state; FAILED may retain quarantined stages. */
typedef enum {
    TIKU_SDIO_DISCOVER, TIKU_SDIO_NEGOTIATE, TIKU_SDIO_READY,
    TIKU_SDIO_QUIESCE, TIKU_SDIO_RESET, TIKU_SDIO_BOOT,
    TIKU_SDIO_FAILED, TIKU_SDIO_CLOSED
} tiku_sdio_state_t;
/** @brief Completion and backing state are separate observations. */
typedef struct { tiku_ipc_err_t status; uint8_t quarantined; } tiku_sdio_result_t;
/** @brief Fixed reserved native control, two bulk slots and one loader slot. */
typedef struct {
    const tiku_sdio_owner_ops_t *ops;
    void *driver;
    const tiku_ipc_access_domain_t *domain;
    tiku_sdio_profile_t profile;
    uint32_t engine, peer, session, serial, stale;
    uint64_t exchange;
    tiku_sdio_mode_t mode, wanted;
    tiku_sdio_state_t state;
    tiku_clock_time_t deadline, lost_at, active_until;
    uint8_t active, cursor, claimed;
    uint8_t native_sequence, native_request[20];
    struct {
        uint8_t tx[TIKU_SDIO_STAGE] __attribute__((aligned(64)));
        uint8_t rx[TIKU_SDIO_STAGE] __attribute__((aligned(64)));
        uint32_t generation;
        tiku_clock_time_t deadline;
        uint16_t tx_length, rx_length;
        tiku_ipc_err_t result;
        uint8_t state, reported;
    } jobs[TIKU_SDIO_JOBS];
} tiku_ipc_sdio_t;
/**
 * @brief Claim one controller and validate stage visibility before any exchange.
 * @note Zero-initialize before first use. Reopen after reusable succeeds with a
 * fresh session, retaining the context's job generations and retired slots.
 */
tiku_ipc_err_t tiku_ipc_sdio_open(tiku_ipc_sdio_t *, const tiku_sdio_owner_ops_t *,
    void *driver, const tiku_ipc_access_domain_t *, uint32_t engine, uint32_t peer,
    uint32_t session, tiku_sdio_mode_t initial, const tiku_sdio_profile_t *);
/** @brief One bounded driver step; native control is selected before queued bulk. */
void tiku_ipc_sdio_step(tiku_ipc_sdio_t *);
/**
 * @brief Copy into an admitted mode/class slot; no caller pointer is retained.
 * @note Native control takes its existing 20-byte request and 32-byte response
 * envelopes. UPDATE uses the loader protocol, without native interpretation.
 */
tiku_ipc_err_t tiku_ipc_sdio_submit(tiku_ipc_sdio_t *, tiku_sdio_class_t,
    const void *data, uint16_t length, uint16_t reply_length,
    tiku_clock_time_t timeout, tiku_sdio_job_t *);
/** @brief Copy a terminal response; uncertain stage access is reported separately. */
tiku_ipc_err_t tiku_ipc_sdio_take(tiku_ipc_sdio_t *, tiku_sdio_job_t,
    void *out, uint16_t capacity, tiku_sdio_result_t *);
/** @brief Discard a result; cancel queued work and retain active stages until final access. */
tiku_ipc_err_t tiku_ipc_sdio_discard(tiku_ipc_sdio_t *, tiku_sdio_job_t);
/** @brief Quiesce before resetting into the selected mode; OFF releases ownership. */
tiku_ipc_err_t tiku_ipc_sdio_mode(tiku_ipc_sdio_t *, tiku_sdio_mode_t);
/** @brief True after close when no job or peripheral can access the context. */
uint8_t tiku_ipc_sdio_reusable(const tiku_ipc_sdio_t *);
#endif
#endif
