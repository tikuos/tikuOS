/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_stream.h - two-chunk window for objects larger than receive memory.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_STREAM_H_
#define TIKU_IPC_STREAM_H_
#include "tiku_ipc.h"

#if TIKU_IPC_ENABLE
#define TIKU_IPC_STREAM_HEADER 28u
#define TIKU_IPC_STREAM_CHUNK (TIKU_IPC_BUFFER_SIZE - TIKU_IPC_STREAM_HEADER)
#if TIKU_IPC_BUFFER_SIZE <= TIKU_IPC_STREAM_HEADER || TIKU_IPC_BUFFER_SIZE > 65535
#error Invalid IPC streaming buffer size
#endif

/** @brief Object admission when a previous object still occupies the stream. */
typedef enum {
    TIKU_IPC_STREAM_PRESERVE, TIKU_IPC_STREAM_DROP_NEW, TIKU_IPC_STREAM_LATEST
} tiku_ipc_stream_policy_t;

/** @brief Caller-owned sender state; zero-initialize before the first open. */
typedef struct {
    tiku_ipc_endpoint_t endpoint;
    tiku_ipc_submission_t pending[2];
    uint32_t object, session, total, offset;
    uint32_t dropped, last_dropped;
    uint8_t failed, replacing, discontinuity;
} tiku_ipc_stream_t;

/** @brief Caller-owned receiver state; zero-initialize before first use. */
typedef struct {
    uint32_t object, session, total, offset;
    uint32_t dropped;
} tiku_ipc_stream_reader_t;

/**
 * @brief Begin a nonempty object; refuse while either previous chunk is held.
 *
 * Object ids increase within a session. State and source pointers are never
 * retained by the kernel; accepted writes use private managed buffers.
 */
tiku_ipc_err_t tiku_ipc_stream_open(tiku_ipc_stream_t *stream,
    tiku_ipc_endpoint_t endpoint, uint32_t object, uint32_t total);
/**
 * @brief Offer an object under preserve, drop-new or latest-object policy.
 * @note LATEST cancels old loans and returns AGAIN until completions are drained.
 * No source bytes are retained. Dropped objects are counted; retry the latest
 * source after OK. DROP_NEW returns CANCELLED when it discards the offered id.
 */
tiku_ipc_err_t tiku_ipc_stream_offer(tiku_ipc_stream_t *stream,
    tiku_ipc_endpoint_t endpoint, uint32_t object, uint32_t total,
    tiku_ipc_stream_policy_t policy);
/** @brief Copy at most CHUNK bytes; AGAIN when the two-chunk window is full. */
tiku_ipc_err_t tiku_ipc_stream_write(tiku_ipc_stream_t *stream,
    const void *data, uint16_t length, tiku_clock_time_t timeout);
/** @brief Account for a completion read through tiku_ipc_completion(). */
tiku_ipc_err_t tiku_ipc_stream_finish(tiku_ipc_stream_t *stream,
    const tiku_ipc_completion_t *completion);
/** @brief Nonzero after every byte succeeded and both chunks were returned. */
uint8_t tiku_ipc_stream_done(const tiku_ipc_stream_t *stream);
/**
 * @brief Validate one received INPUT chunk and expose its payload until ack.
 *
 * Checks CRC-32, session/object order, total, offset, length and final marker.
 * Successful reads advance reader offset. The application consumes bytes and
 * acks the submission; on error it acks failure.
 * @note Atomic publication requires application storage for the complete object.
 */
tiku_ipc_err_t tiku_ipc_stream_read(tiku_ipc_stream_reader_t *reader,
    tiku_ipc_endpoint_t endpoint, const tiku_ipc_received_t *message,
    const void **data, uint16_t *length);
/** @brief Validate a copied transport chunk; the borrowed payload lasts with memory. */
tiku_ipc_err_t tiku_ipc_stream_decode(tiku_ipc_stream_reader_t *reader,
    const void *memory,uint16_t bytes,const void **data,uint16_t *length);
#endif
#endif
