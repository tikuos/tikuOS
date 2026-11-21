/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_listener.h - bounded stream service and controller backend.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_TRANSFER_LISTENER_H_
#define TIKU_MEDIA_TRANSFER_LISTENER_H_
#include "tiku_media_transfer_control.h"
#include "tiku_media_transfer_async.h"
/** @brief Serialized owner callbacks; admission and cleanup must not wait for hardware.
 * @note Listen returns zero complete or BUSY without admission. Unlisten returns zero
 * after endpoint removal and auxiliary cleanup, or BUSY with cleanup retained.
 * Read/send return copied bytes, zero/BUSY for backpressure, or a negative error.
 * Send copies its input before returning. Retire returns 1 pending, zero released
 * or BUSY with ownership retained. Accepted connections remain reserved through
 * disconnect until retire returns zero. Cancel retires the specified media request
 * or returns BUSY; the existing media owners continue to run independently.
 */
typedef struct {
    int (*in_owner)(void *);
    int (*listen)(void *,uint16_t);
    int (*unlisten)(void *,uint16_t);
    int (*read)(void *,void *connection,void *,size_t);
    int (*send)(void *,void *connection,const void *,size_t);
    int (*retire)(void *,void *connection);
    int (*cancel)(void *,tiku_media_request_t *);
} tiku_mt_listener_ops_t;
/** @brief Caller-owned service state and session buffers. */
typedef struct tiku_mt_listener tiku_mt_listener_t;
/** @brief Copied service state; READY requires a listening endpoint and initialized session. */
typedef struct {
    uint64_t accepted,committed,upload,expected,read_offset,read_length;
    unsigned ready,connected,recovering,reading,fault;
    int error;
} tiku_mt_listener_snapshot_t;
/** @brief Required caller storage size including packet and staging buffers. */
size_t tiku_mt_listener_size(void);
/** @brief Required caller storage alignment. */
size_t tiku_mt_listener_align(void);
/** @brief Initialize fixed metadata without clearing session buffers or performing I/O.
 * @note Store, ops context and storage outlive controller commands and connections.
 * Client is nonzero and reserved for this service. Storage is unused; reinitializing
 * a live service is invalid. Calls, events and media progression share one owner.
 */
tiku_mt_listener_t *tiku_mt_listener_init(void *,size_t,tiku_media_store_t *,uint32_t,
                                       tiku_mt_listener_ops_t,void *);
/** @brief Controller owner predicate; returns zero outside the bound service owner. */
int tiku_mt_listener_in_owner(void *);
/** @brief Controller backend; one transport callback or one initialization/cleanup phase.
 * @note Profile storage remains immutable through Stop completion. Enable validates
 * the mounted volume. Stop preempts reconnection, retires borrowed requests and
 * connections, releases client handles, then erases at most 512 session bytes per call.
 */
int tiku_mt_listener_phase(void *,tiku_mt_control_phase_t,const tiku_mt_control_profile_t *);
/** @brief Accept one reserved connection; BUSY retains nothing and the caller retires it. */
int tiku_mt_listener_connected(tiku_mt_listener_t *,void *connection,uint32_t now_ms);
/** @brief Mark the accepted connection disconnected while retaining its transport reservation. */
int tiku_mt_listener_closed(tiku_mt_listener_t *,void *connection);
/** @brief Advance one stream or recovery phase; call separately from media/network owners.
 * @note Transfers use four-byte little-endian packet lengths and at most 512 bytes
 * per transport call. Inactivity of 120000 ms triggers session cleanup and relisten.
 * Recovery failure retains resources for Stop and reports not ready. The controller
 * describes command completion; observe this service to determine current readiness.
 */
int tiku_mt_listener_step(tiku_mt_listener_t *,uint32_t now_ms);
/** @brief Copy service state without progressing transport or media work. */
int tiku_mt_listener_observe(tiku_mt_listener_t *,tiku_mt_listener_snapshot_t *);
#endif
