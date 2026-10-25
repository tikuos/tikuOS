/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc.h - bounded endpoints, copied messages and managed buffer loans.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_H_
#define TIKU_IPC_H_

#include "tiku_ipc_config.h"
#include "../process/tiku_process.h"
#include <stddef.h>
#include <stdint.h>

#if TIKU_IPC_ENABLE
/** @brief Handle types use separate generation namespaces and tables. */
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_ipc_endpoint_t;
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_ipc_buffer_t;
typedef struct { uint32_t generation; uint16_t slot_plus_one; } tiku_ipc_submission_t;

/** @brief Zero is success; AGAIN leaves the caller's work unaccepted. */
typedef enum {
    TIKU_IPC_OK = 0, TIKU_IPC_AGAIN = -1, TIKU_IPC_INVALID = -2,
    TIKU_IPC_BUSY = -3, TIKU_IPC_DOWN = -4, TIKU_IPC_UNSUPPORTED = -5,
    TIKU_IPC_CANCELLED = -6, TIKU_IPC_TIMEOUT = -7, TIKU_IPC_IO = -8
} tiku_ipc_err_t;

/** @brief INPUT loans are read-only; OUTPUT is valid only after success. */
typedef enum { TIKU_IPC_INPUT, TIKU_IPC_OUTPUT } tiku_ipc_direction_t;

/** @brief The resources invalidated by an explicitly requested reset. */
typedef enum {
    TIKU_IPC_RESET_NONE, TIKU_IPC_RESET_ENDPOINT,
    TIKU_IPC_RESET_CONNECTION, TIKU_IPC_RESET_ENGINE
} tiku_ipc_reset_scope_t;

#define TIKU_IPC_CAP_COPY   0x01u
#define TIKU_IPC_CAP_LOAN   0x02u
#define TIKU_IPC_CAP_STREAM 0x04u
#define TIKU_IPC_CAP_CONTROL 0x08u

/** @brief Synchronous native control; callbacks retain neither pointer. */
typedef struct {
    const void *input;
    void *output;
    uint32_t input_length, output_capacity, value;
} tiku_ipc_control_t;

/** @brief A received message or loan; ack a loan after the final access. */
typedef struct {
    uint16_t length;
    uint8_t is_buffer;
    tiku_ipc_direction_t direction;
    tiku_ipc_submission_t submission;
    uint8_t data[TIKU_IPC_MESSAGE_MAX];
} tiku_ipc_received_t;

/** @brief Terminal outcome; quarantined backing remains unavailable. */
typedef struct {
    tiku_ipc_submission_t submission;
    tiku_ipc_err_t status;
    uint8_t quarantined;
} tiku_ipc_completion_t;

/** @brief Per-endpoint limits and current bounded resource use. */
typedef struct {
    uint16_t max_message, depth, queued, credits, high_water;
    uint16_t max_buffer, alignment, methods;
    uint16_t buffers_live, buffers_free, buffers_quarantined;
    uint32_t session, sent, delivered, drops, stale, cancelled;
    uint32_t invalid_handles_global; /**< Rejected handle lookups across IPC tables. */
    uint32_t wire_bad, wire_stale, wire_duplicates, wire_failed;
    uint32_t transport_bytes, transport_crc;
    uint32_t peer_service_max_gap, peer_service_calls;
    uint16_t transport_inflight;
    uint8_t transport_stats;
    uint8_t open, pending_wakeup, reset_supported;
    tiku_ipc_reset_scope_t reset_scope;
} tiku_ipc_status_t;

struct tiku_ipc_endpoint_config;

/**
 * @brief Initialize at boot, after timers and memory, with no current process.
 *
 * Configuration has static lifetime. A second initialization is refused.
 * Count zero is valid. No caller-buffer registration is provided.
 */
tiku_ipc_err_t tiku_ipc_init(const struct tiku_ipc_endpoint_config *table,
                            uint16_t count);
/** @brief Bind an unused named endpoint to the current running instance. */
tiku_ipc_err_t tiku_ipc_open(const char *name, tiku_ipc_endpoint_t *out);
/** @brief Close the caller's binding and cancel its work; retain active loans. */
tiku_ipc_err_t tiku_ipc_close(tiku_ipc_endpoint_t endpoint);
/** @brief Copy a message to the configured destination; never retain data. */
tiku_ipc_err_t tiku_ipc_send(tiku_ipc_endpoint_t endpoint,
                            const void *data, uint16_t length);
/** @brief Pop one message or loan, or AGAIN; a short destination is not used. */
tiku_ipc_err_t tiku_ipc_receive(tiku_ipc_endpoint_t endpoint,
                               tiku_ipc_received_t *out);
/** @brief Allocate caller-owned storage from the private TRANSIENT pool. */
tiku_ipc_err_t tiku_ipc_buffer_alloc(uint16_t length, tiku_ipc_buffer_t *out);
/** @brief Map caller storage; writable mapping is refused during a loan. */
tiku_ipc_err_t tiku_ipc_buffer_map(tiku_ipc_buffer_t buffer,
                                  void **data, uint16_t *length);
/** @brief Drop caller ownership; an operation retains its independent loan. */
tiku_ipc_err_t tiku_ipc_buffer_release(tiku_ipc_buffer_t buffer);
/**
 * @brief Loan a buffer until completion or confirmed quiescence.
 *
 * Timeout is in ticks, between 1 and TIKU_CLOCK_MAX_INTERVAL. An accepted
 * operation owns a reference even after caller release or process exit.
 */
tiku_ipc_err_t tiku_ipc_submit(tiku_ipc_endpoint_t endpoint,
    tiku_ipc_buffer_t buffer, tiku_ipc_direction_t direction,
    tiku_clock_time_t timeout, tiku_ipc_submission_t *out);
/** @brief Map a received INPUT loan without granting write ownership. */
tiku_ipc_err_t tiku_ipc_loan_read(tiku_ipc_endpoint_t endpoint,
    tiku_ipc_submission_t submission, const void **data, uint16_t *length);
/** @brief Map a received OUTPUT loan with exclusive write ownership. */
tiku_ipc_err_t tiku_ipc_loan_write(tiku_ipc_endpoint_t endpoint,
    tiku_ipc_submission_t submission, void **data, uint16_t *length);
/** @brief End the receiver's access; only OK publishes OUTPUT bytes. */
tiku_ipc_err_t tiku_ipc_ack(tiku_ipc_endpoint_t endpoint,
    tiku_ipc_submission_t submission, tiku_ipc_err_t status);
/** @brief Request cancellation; active hardware access may be quarantined. */
tiku_ipc_err_t tiku_ipc_cancel(tiku_ipc_endpoint_t endpoint,
                              tiku_ipc_submission_t submission);
/** @brief Read one terminal completion; AGAIN when no unreported one exists. */
tiku_ipc_err_t tiku_ipc_completion(tiku_ipc_endpoint_t endpoint,
                                  tiku_ipc_completion_t *out);
/** @brief Query by boot-table index; does not require application ownership. */
tiku_ipc_err_t tiku_ipc_status(uint16_t index, tiku_ipc_status_t *out);
/** @brief Query limits after validating the caller's endpoint binding. */
tiku_ipc_err_t tiku_ipc_query(tiku_ipc_endpoint_t endpoint, tiku_ipc_status_t *out);
/** @brief Boot-table count and names; names remain valid for the boot. */
uint16_t tiku_ipc_endpoint_count(void);
const char *tiku_ipc_endpoint_name(uint16_t index);

/** @brief Request a backend reset by table index; authorization is the caller's. */
tiku_ipc_err_t tiku_ipc_reset(uint16_t index);
/** @brief Invoke a boot-registered native control operation by endpoint index. */
tiku_ipc_err_t tiku_ipc_control(uint16_t index, uint16_t operation,
                               tiku_ipc_control_t *request);
/**
 * @brief Install a boot-lifetime observer called after kernel progress.
 * @note Kernel context, no current process; one observer, no recursive progress.
 */
tiku_ipc_err_t tiku_ipc_observe(void (*callback)(void *), void *context);

/** @brief Bounded kernel service; recursive backend pumping is refused. */
void tiku_ipc_progress(uint16_t budget);
/** @brief IRQ-safe immediate readiness; future deadlines permit idle. */
uint8_t tiku_ipc_pending(void);
#endif
#endif
