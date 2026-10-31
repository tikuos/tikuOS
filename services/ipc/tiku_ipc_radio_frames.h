/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_radio_frames.h - bounded complete-frame queues over managed IPC streams.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IPC_RADIO_FRAMES_H_
#define TIKU_IPC_RADIO_FRAMES_H_
#include <interfaces/wireless/tiku_wireless_ipc.h>
#include <kernel/ipc/tiku_ipc_stream.h>
#ifndef TIKU_RADIO_LOCAL_QUEUE
#define TIKU_RADIO_LOCAL_QUEUE TIKU_RADIO_QUEUE
#endif
#if TIKU_RADIO_LOCAL_QUEUE < 2 || TIKU_RADIO_LOCAL_QUEUE > 4
#error "Radio frame queues require two to four slots"
#endif
/** @brief Bounded frame slots; an incomplete receive occupies one slot. */
typedef struct {
    uint8_t data[TIKU_RADIO_LOCAL_QUEUE][TIKU_RADIO_FRAME_MAX];
    uint16_t length[TIKU_RADIO_LOCAL_QUEUE];
    uint8_t complete[TIKU_RADIO_LOCAL_QUEUE], interface[TIKU_RADIO_LOCAL_QUEUE], head, count;
    uint8_t tagged;
    uint32_t drops, delivered, bad;
    tiku_ipc_stream_reader_t reader;
    tiku_clock_time_t deadline;
    uint8_t assembling, assembly;
} tiku_radio_frames_t;
/** @brief Sender loans retain managed copies; frame storage remains caller-owned. */
typedef struct {
    tiku_radio_frames_t frames;
    tiku_ipc_stream_t stream;
    uint32_t serial;
    uint8_t active;
} tiku_radio_sender_t;
/** @brief Queue a frame for interface zero or one; tagged streams encode it in the object id. */
tiku_ipc_err_t tiku_radio_frame_push_if(tiku_radio_frames_t *, uint8_t, const void *, uint16_t);
/** @brief Copy a whole frame, returning AGAIN without accepting bytes when full. */
tiku_ipc_err_t tiku_radio_frame_push(tiku_radio_frames_t *, const void *, uint16_t);
/** @brief Borrow the oldest complete frame until pop; NULL while empty or assembling. */
const uint8_t *tiku_radio_frame_peek(tiku_radio_frames_t *, uint16_t *);
/** @brief Release the oldest complete frame after the consumer's final access. */
void tiku_radio_frame_pop(tiku_radio_frames_t *);
/** @brief Decode one managed stream chunk and publish only a complete frame. */
tiku_ipc_err_t tiku_radio_frame_consume(void *, const void *, uint16_t);
/** @brief Drop an expired partial frame while preserving object ordering. */
void tiku_radio_frame_expire(tiku_radio_frames_t *, tiku_clock_time_t);
/** @brief Progress completions and at most one chunk in the endpoint owner's process. */
void tiku_radio_sender_step(tiku_radio_sender_t *, tiku_ipc_endpoint_t);
/** @brief Cancel active loans; call step until active clears before resetting storage. */
void tiku_radio_sender_cancel(tiku_radio_sender_t *, tiku_ipc_endpoint_t);
#endif
