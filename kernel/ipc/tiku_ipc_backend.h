/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_backend.h - kernel-to-transport submission and quiescence contract.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_BACKEND_H_
#define TIKU_IPC_BACKEND_H_
#include "tiku_ipc.h"

#if TIKU_IPC_ENABLE
/** @brief Backend operation snapshot; data is held until quiescent completion. */
typedef struct {
    tiku_ipc_submission_t submission;
    uint32_t session;
    void *data;
    uint16_t length;
    tiku_ipc_direction_t direction;
} tiku_ipc_transfer_t;

/**
 * @brief Transport callbacks run from progress with interrupts enabled.
 *
 * start returning OK transfers access; other results retain no access.
 * quiesce confirms final access has ended; zero quarantines backing.
 * progress rearms future polling. No callback dispatches applications or IPC.
 */
typedef struct {
    tiku_ipc_err_t (*send)(void *context, uint16_t endpoint, uint32_t session,
                          const void *data, uint16_t length);
    tiku_ipc_err_t (*start)(void *context, uint16_t endpoint,
                           const tiku_ipc_transfer_t *transfer);
    void (*progress)(void *context, uint16_t endpoint, uint16_t budget);
    uint8_t (*quiesce)(void *context, uint16_t endpoint,
                       const tiku_ipc_transfer_t *transfer);
    uint16_t max_message, max_buffer; /**< Zero selects the core's limit. */
    /** Copy cached counters with IRQs masked; no hardware access or dispatch. */
    void (*status)(void *context, tiku_ipc_status_t *out);
    /** Accept a reset; retain access fences until each transfer is quiescent. */
    tiku_ipc_err_t (*reset)(void *context, uint16_t endpoint);
    tiku_ipc_reset_scope_t reset_scope;
} tiku_ipc_backend_ops_t;

/**
 * @brief Boot endpoint configuration.
 *
 * NULL ops selects local delivery to peer's table index. Names are distinct
 * relative paths with no endpoint as an ancestor; context and ops are static.
 * Backends validate access domains; pools grant no device visibility.
 */
typedef struct tiku_ipc_endpoint_config {
    const char *name;
    const tiku_ipc_backend_ops_t *ops;
    void *context;
    uint16_t peer;
} tiku_ipc_endpoint_config_t;

/** @brief Table defined by boot assembly, never by the kernel transport core. */
extern const tiku_ipc_endpoint_config_t tiku_ipc_backend_table[];
extern const uint16_t tiku_ipc_backend_count;

/** @brief Bounded native driver control, with no retained caller pointers. */
typedef tiku_ipc_err_t (*tiku_ipc_control_fn)(void *, uint16_t, tiku_ipc_control_t *);
/** @brief Register one native control provider per endpoint during boot. */
tiku_ipc_err_t tiku_ipc_control_bind(uint16_t endpoint,
    tiku_ipc_control_fn call, void *context);

/** @brief Request service after delay ticks; zero requests immediate progress. */
tiku_ipc_err_t tiku_ipc_service_at(uint16_t endpoint, tiku_clock_time_t delay);
/** @brief Remove a backend's poll/retry deadline after it becomes inactive. */
tiku_ipc_err_t tiku_ipc_service_stop(uint16_t endpoint);
/** @brief IRQ-safe copied ingress; wrong session or full queue is refused. */
tiku_ipc_err_t tiku_ipc_deliver(uint16_t endpoint, uint32_t session,
                               const void *data, uint16_t length);
/**
 * @brief Complete a transfer after confirmed quiescence and cache sync.
 *
 * Full ticket and captured session must match, including for a quarantined
 * transfer. A late completion cannot release another generation's backing.
 */
tiku_ipc_err_t tiku_ipc_complete(tiku_ipc_submission_t submission,
    uint32_t session, tiku_ipc_err_t status);
/** @brief Invalidate peer work; active loans require transfer-scoped quiescence. */
tiku_ipc_err_t tiku_ipc_peer_reset(uint16_t endpoint);
#endif
#endif
